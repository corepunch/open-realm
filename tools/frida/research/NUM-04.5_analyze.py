#!/usr/bin/env python3
"""Analyze NUM-04.5/NUM-04.6 startup random-state captures (NUM-04.5_trace.py observe output).

For each capture: ordered startup timeline (setup flags/seed decision, owner and 45-stream seeds, every owner draw
and stream draw with caller, script markers), then an injection-free model check: starting only from the setup
seed rule (MAP_LOCK_RANDOM_SEED -> 0x77617233, else the setup-record seed) and the map's race preferences, the
Python port of the 1.27 generator must reproduce every recorded owner/stream word in order. Also compares the
JASS Preload outputs of observe/control/repeat runs (observer-free control).
"""
import argparse, hashlib, json, re, sys
from collections import Counter, OrderedDict
from pathlib import Path

WORDS = None


def gen():
    g = {}
    exec(open('/GitHub/wc3-analysis/reports/pathfinding-1.27/research/_ghidra/pe.py').read().split('if __name__')[0], g)
    return g['u32']


def load_words():
    global WORDS
    if WORDS is None:
        u32 = gen(); WORDS = [u32(0x6fa92f10 + 4 * i) for i in range(61)]
    return WORDS


def seed(s):
    s &= 0xffffffff
    return [s, ((s % 59) * 0x400 | (s % 61) * 4 | (s % 53) * 0x40000 | (((s // 47) * 17 + s) * 0x4000000)) & 0xffffffff]


def nxt(st):
    table = load_words()
    shifts, steps, periods = (24, 16, 8, 0), (4, 12, 24, 28), (188, 212, 236, 244)
    index = mix = 0
    for i in range(4):
        off = ((st[1] >> shifts[i]) & 255) - steps[i]
        if off < 0:
            off += periods[i]
        w = table[off // 4]
        rot = i + 1 if i < 3 else 0
        mix ^= ((w << rot) | (w >> (32 - rot))) & 0xffffffff if rot else w
        index |= off << shifts[i]
    st[1] = index; st[0] = (st[0] + mix) & 0xffffffff
    return st[0]


def hx(c):
    return hex(c) if isinstance(c, int) else c


def reseed45(s):
    loc = seed(s); return [seed(nxt(loc)) for _ in range(45)]


def analyze(path):
    rows = [json.loads(l) for l in open(path)]
    ev = [r for r in rows if r.get('event') not in ('metadata', 'trace-end', 'loading-key')]
    end = next((r for r in rows if r.get('event') == 'trace-end'), None)
    dec = next((r for r in ev if r['event'] == 'setup-seed-decision'), None)
    if dec is None:
        return None  # not a NUM-04.5 observer capture (preload comparison only)
    locked = bool(dec['flags'] & 0x8000)
    game_seed = 0x77617233 if locked else dec['lobbySeed']
    owner = seed(game_seed); streams = reseed45(game_seed); expected_reseed_word = game_seed
    checks = Counter(); mism = []
    timeline = []
    started = False
    for r in ev:
        e = r['event']
        if e == 'seed' and r['cls'] == 'owner' and r['caller'] in (0x6f29ec20, 0x6f29ec33):
            started = True
            ok = r['seed'] == game_seed and r['after'] == owner
            checks['owner-seed-ok' if ok else 'owner-seed-bad'] += 1
            timeline.append(dict(seq=r['seq'], what='owner-seed', seed=r['seed'], after=r['after'], caller=hx(r['caller'])))
            continue
        if not started:
            if e in ('setup-descriptor', 'setup-descriptor-leave', 'setup-enter', 'setup-seed-decision', 'setup-record-tick', 'jass-config-setmapname'):
                timeline.append(dict(seq=r['seq'], what=e, **{k: r[k] for k in r if k not in ('event', 'seq', 'ms')}))
            continue
        if e == 'seed' and r['cls'].startswith('stream:'):
            i = int(r['cls'].split(':')[1])
            ok = r['after'] == streams[i] or None
            if r['caller'] == 0x6f6937dd:  # reseed loop; streams[] already reflects the expected value
                checks['stream-seed-ok' if r['after'] == streams[i] else 'stream-seed-bad'] += 1
            continue
        if e == 'streams-reseed':
            ok = r['streams'] == streams and r['seed'] == expected_reseed_word
            checks['reseed-ok' if ok else 'reseed-bad'] += 1
            timeline.append(dict(seq=r['seq'], what='streams-reseed', seed=r['seed'], caller=hx(r['caller']), match=ok))
            continue
        if e == 'jass-setrandomseed':  # 214140: owner=Seed(s); one owner draw; 693710(draw) reseeds all 45 streams
            owner = seed(r['seed']); expected_reseed_word = nxt(owner[:]); streams = reseed45(expected_reseed_word)
            timeline.append(dict(seq=r['seq'], what='SetRandomSeed', seed=r['seed'], expected_reseed_word=expected_reseed_word))
            continue
        if e == 'seed' and r['cls'] == 'owner':  # 214140 Seed: model already reseeded at jass-setrandomseed
            checks['owner-reseed-ok' if r['after'] == owner else 'owner-reseed-bad'] += 1
            continue
        if e == 'draw':
            if r['cls'] == 'owner':
                b = owner[:]; v = nxt(owner)
                ok = r['before'] == b and r['after'] == owner and r['value'] == v
                if not ok:
                    mism.append(dict(seq=r['seq'], cls='owner', model_before=b, live_before=r['before']))
                    owner = r['after'][:]
            else:
                i = int(r['cls'].split(':')[1]); b = streams[i][:]; v = nxt(streams[i])
                ok = r['before'] == b and r['after'] == streams[i] and r['value'] == v
                if not ok:
                    mism.append(dict(seq=r['seq'], cls=r['cls'], model_before=b, live_before=r['before']))
                    streams[i] = r['after'][:]
            checks[r['cls'].split(':')[0] + ('-draw-ok' if ok else '-draw-bad')] += 1
            timeline.append(dict(seq=r['seq'], what='draw', cls=r['cls'], caller=hx(r['caller']),
                                 before=r['before'], after=r['after'], value=r['value'], visit=r['visit'], phase=r['phase']))
            continue
        if e in ('first-owner-visit', 'first-mover-update', 'separation-visit'):
            okw = r['owner'] == owner
            okS = r['streams'] is None or r['streams'] == streams
            checks[e + ('-owner-ok' if okw else '-owner-bad')] += 1
            if r['streams'] is not None:
                checks[e + ('-streams-ok' if okS else '-streams-bad')] += 1
            timeline.append(dict(seq=r['seq'], what=e, owner=r['owner'], visit=r['visit'],
                                 streams_changed=None if r['streams'] is None else [i for i in range(45) if r['streams'][i] != reseed45(game_seed)[i]]))
            continue
        if e in ('marker', 'resolve-races', 'jass-main-setcamerabounds', 'jass-config-setmapname', 'jass-setmapflag', 'setup-leave', 'streams-table',
                 'jass-setplayerracepreference', 'replay-setup-enter'):
            d = {k: r[k] for k in r if k not in ('event', 'seq', 'ms')}
            if e == 'marker':
                checks['marker-owner-ok' if r['owner'] == owner else 'marker-owner-bad'] += 1
            timeline.append(dict(seq=r['seq'], what=e, **d))
    races = [t for t in timeline if t['what'] == 'draw' and t['caller'] == '0x6f1e9e25']
    callers = Counter((t['cls'].split(':')[0] if t['cls'] == 'owner' else t['cls'], t['caller'], t['phase']) for t in timeline if t['what'] == 'draw')
    return dict(capture=str(path), capture_sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(), locked=locked, flags=dec['flags'],
                lobby_seed=dec['lobbySeed'], game_seed=game_seed, race_draws=len(races), checks=dict(checks), mismatches=mism[:50],
                mismatch_count=len(mism), draw_callers=[dict(cls=k[0], caller=k[1], phase=k[2], n=v) for k, v in sorted(callers.items())],
                agg=end.get('agg') if end else None, timeline=timeline)


def preload_lines(p):
    if not p.exists():
        return None
    return re.findall(r'call Preload\( "([A-Z]+ [^"\r\n]*)" \)', p.read_bytes().decode('utf-8', 'replace'))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', nargs='+', type=Path, help='capture directories (observe or control)')
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    res = OrderedDict()
    pre = OrderedDict()
    for d in a.captures:
        key = d.parent.parent.name + '/' + d.name
        if (d / 'capture.jsonl').exists():
            r = analyze(d / 'capture.jsonl')
            if r is not None:
                res[key] = r
        pre[key] = preload_lines(d / 'preload.txt')
    names = list(pre)
    comparisons = []
    for i in range(len(names)):
        for j in range(i + 1, len(names)):
            x, y = pre[names[i]], pre[names[j]]
            if x is None or y is None:
                continue
            strip = lambda L: [re.sub(r' h=\d+', '', s) for s in L]
            comparisons.append(dict(a=names[i], b=names[j], lines=(len(x), len(y)), identical=x == y,
                                    identical_without_handles=strip(x) == strip(y)))
    out = dict(analyzer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), captures=res, preload=pre, preload_comparisons=comparisons)
    a.out.write_text(json.dumps(out, indent=1) + '\n')
    for k, v in res.items():
        print(k, 'locked', v['locked'], 'seed', hex(v['game_seed']), 'races', v['race_draws'], 'checks', v['checks'], 'mismatches', v['mismatch_count'])
    for c in comparisons:
        print('preload', c)


if __name__ == '__main__':
    main()
