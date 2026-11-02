#!/usr/bin/env python3
"""FORM-04.2 route-failure / warp-marker analyzer (research tool, new file).

Reads form05/form052 observer captures and reports, per physical group (pointer + identity words):
  * route requests (16ce10): every transition of (result, ready, final, accCount, accIndex, path88 flags), all result-0
    failures and every 16c5d0 StopMembers with owner visit / probe tick;
  * classification-denial bookkeeping: visits where the group68 cooldown is (re)seeded (snapshot value 65 = 66 set by
    16c250 then decremented after commit) together with the member28 bits e0000 present on that visit
    (20000 forced, 40000 adjusted, 80000 warp marker mirrored from path88 01000000 by 16a790);
  * per unit, the visit intervals carrying member bit 80000;
  * warps (165f10 nonzero) attributed to units via member path pointers, with the path words before/after;
  * layouts, advances and resets (visit, caller).
Units are identified as in form05_analyze.py (latest probe marker positions; u6 = runner in variant e).
"""
import argparse, hashlib, json, re, struct, bisect
from pathlib import Path

f32 = lambda w: struct.unpack('<f', struct.pack('<I', w & 0xffffffff))[0]
UNIT_RE = re.compile(r' u(\d+)=(-?[\d.]+),(-?[\d.]+),(\d+)')


def analyze(path, prefix='F05 '):
    rows = [json.loads(l) for l in Path(path).read_text().splitlines() if l.strip()]
    markers = []
    for r in rows:
        if r.get('event') == 'marker' and r['value'].startswith(prefix):
            t = int(re.search(r'tick=(\d+)', r['value']).group(1))
            markers.append((r['seq'], t, {int(m.group(1)): (float(m.group(2)), float(m.group(3))) for m in UNIT_RE.finditer(r['value'])}))
    seqs = [m[0] for m in markers]

    def mk(seq):
        i = bisect.bisect_left(seqs, seq) - 1
        return markers[i] if i >= 0 else (0, -1, {})
    ident, pathunit = {}, {}

    def unit(seq, mover, pos):
        if mover in ident:
            return ident[mover]
        m = mk(seq)
        if not pos or not m[2]:
            return None
        wx, wy = f32(pos[0]) * 32, f32(pos[1]) * 32
        u, p = min(m[2].items(), key=lambda kv: (kv[1][0] - wx) ** 2 + (kv[1][1] - wy) ** 2)
        if (p[0] - wx) ** 2 + (p[1] - wy) ** 2 < 48 ** 2:
            ident[mover] = u
        return ident.get(mover)
    groups, cur = {}, {}

    def G(key):
        return groups.setdefault(key, dict(group=key, visits=0, route=[], failures=[], stops=[], cooldown_seeds=[], bit80000={},
                                           layouts=[], advances=[], resets=[], first_tick=None, last_tick=None))
    for r in rows:
        ev = r.get('event')
        if 'seq' not in r:
            continue
        t = mk(r['seq'])[1]
        if ev == 'tick':
            key = '%s#%d/%d' % (r['group'], r['identity'][0], r['identity'][1])
            cur[r['group']] = key
            g = G(key)
            g['visits'] += 1
            v = g['visits']
            g['first_tick'] = t if g['first_tick'] is None else g['first_tick']
            g['last_tick'] = t
            bits = []
            for m in r['members']:
                if 'mover' not in m:
                    continue
                u = unit(r['seq'], m['mover'], m['pos'])
                pathunit[m['path']] = u
                fl = int(m['flags'], 16)
                bits.append([u, '%x' % (fl & 0xe0000)])
                if fl & 0x80000:
                    iv = g['bit80000'].setdefault(str(u), [])
                    if iv and iv[-1][1] == v - 1:
                        iv[-1][1] = v
                    else:
                        iv.append([v, v])
            if r['cooldown'] == 65:
                g['cooldown_seeds'].append([v, t, [b for b in bits if b[1] != '0']])
            g['_bits'] = bits
        elif ev in ('route', 'stop-members', 'layout', 'advance', 'reset-members') and r['group'] in cur:
            g = G(cur[r['group']])
            v = g['visits']
            if ev == 'route':
                p = r['path'] or {}
                sig = [r['result'], r['ready'], r['final'], p.get('accCount'), p.get('accIndex'), p.get('flags')]
                if not g['route'] or g['route'][-1][2:] != sig:
                    g['route'].append([v, t] + sig)
                if r['result'] == 0:
                    g['failures'].append([v, t, p.get('retry'), p.get('accTime'), p.get('fineTime')])
            elif ev == 'stop-members':
                g['stops'].append([v, t, r['caller']])
            elif ev == 'layout':
                g['layouts'].append([v, t, hex(r['heading']), [round(f32(x), 6) for x in r['point']]])
            elif ev == 'advance':
                g['advances'].append([v, t, r['caller'], r['resetMembers']])
            else:
                g['resets'].append([v, t, r['caller']])
    warps = []
    for r in rows:
        if r.get('event') == 'warp':
            b, a = r['before'], r['after']
            warps.append(dict(tick=mk(r['seq'])[1], unit=pathunit.get(b['path']), caller=r['caller'],
                              before=[b['accCount'], b['accIndex'], b['fineCount'], b['fineIndex'], b['flags']],
                              after=[a['accCount'], a['accIndex'], a['fineCount'], a['fineIndex'], a['flags']]))
    for g in groups.values():
        g.pop('_bits', None)
    return dict(capture=Path(path).name, sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(), groups=list(groups.values()),
                warps=warps, identity=ident)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', nargs='+', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    res = [analyze(p) for p in args.captures]
    args.output.write_text(json.dumps(res, indent=1) + '\n')
    for x in res:
        print('==', x['capture'], 'warps', [(w['tick'], w['unit']) for w in x['warps']])
        for g in x['groups']:
            print(' ', g['group'], 'visits', g['visits'], 'ticks', g['first_tick'], g['last_tick'], 'route', g['route'][:6], 'failures', len(g['failures']),
                  'stops', len(g['stops']), 'seeds', [(s[0], s[2]) for s in g['cooldown_seeds']][:8], 'bit80000', g['bit80000'],
                  'layouts', [(l[0], l[3]) for l in g['layouts']], 'adv', [(a[0], a[2]) for a in g['advances']])
    print(hashlib.sha256(args.output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
