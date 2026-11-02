#!/usr/bin/env python3
"""MAP-02.2: freeze expected cells / support results from complete captures and check repeats.

Usage: map022_expected.py --report-dir DIR  (reads DIR/captures/*.jsonl, writes DIR/expected-MAP-02.2.json)
"""
import argparse, collections, hashlib, json, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from map022_summarize import summarize  # noqa: E402

LANES = ('walk02', 'fly04', 'build08', 'bit10', 'blight20', 'float40', 'amph80')
POINTS = {'cliff_bottom': (640.0, 1856.0), 'cliff_top': (1408.0, 1856.0), 'dry': (256.0, 192.0), 'shore': (448.0, 192.0),
          'shallow': (576.0, 192.0), 'deep': (1024.0, 192.0), 'bridge_deck': (1024.0, 640.0), 'bridge_shallow': (576.0, 640.0)}


def rows(path):
    return [json.loads(l) for l in open(path)]


def markers(path):
    return [r['value'] for r in rows(path) if r.get('event') == 'marker']


def preload_markers(path):
    return re.findall(r'call Preload\( "(MAP022 [^"\r\n]*)" \)', Path(path).read_text('utf-8'))


def lanes(byte):
    return {name: bool(byte & bit) for name, bit in zip(LANES, (2, 4, 8, 0x10, 0x20, 0x40, 0x80))}


def cell_view(snapshot, name):
    x, y = POINTS[name]
    fx, fy = int(x // 32), int(y // 32)
    w = snapshot['width']
    word = snapshot['words'][fy * w + fx]
    linked = next((l['records'] for l in snapshot['linked'] if l['cell'] == fy * w + fx), [])
    hier = []
    for h in snapshot['hierarchy']:
        s = h['level'] + 1
        hw = h['words'][(fy >> s) * h['width'] + (fx >> s)]
        # Lane shift k (6fce4570 masks: ground 06 k=0, amph 80 k=2, float 40 k=4, fly 04 k=6) stores its
        # class (0 clear, 1 blocked, 2 mixed) at bits 30-k..31-k (same decoding as wc3_pathfinding.js).
        hier.append(dict(level=h['level'], cell=[fx >> s, fy >> s], word='%08x' % hw,
                         classes={lane: (hw >> (30 - k)) & 3 for k, lane in zip((0, 2, 4, 6), ('ground', 'amph', 'float', 'fly'))}))
    return dict(fine=[fx, fy], top='%02x' % (word >> 24), lanes_blocked=lanes(word >> 24),
                objects=sorted({'%06x' % (r['category'] & 0xffffff) for r in linked if r['kind'] == 1}), hierarchy=hier)


def snapshot_digest(snapshot):
    top = bytes(w >> 24 for w in snapshot['words'])
    return dict(width=snapshot['width'], height=snapshot['height'], top_byte_sha256=hashlib.sha256(top).hexdigest(),
                histogram={'%02x' % k: v for k, v in sorted(collections.Counter(top).items())},
                hierarchy_sha256=hashlib.sha256(json.dumps([h['words'] for h in snapshot['hierarchy']]).encode()).hexdigest())


def objects(snapshot):
    objs = collections.defaultdict(set)
    for l in snapshot['linked']:
        for r in l['records']:
            if r['kind'] == 1 and r['category'] & 0xffffff:
                objs['%06x' % (r['category'] & 0xffffff)].add(l['cell'])
    out = {}
    for cat, cells in sorted(objs.items()):
        xs = [c % 64 for c in cells]; ys = [c // 64 for c in cells]
        out[cat] = dict(cells=len(cells), x=[min(xs), max(xs)], y=[min(ys), max(ys)],
                        cells_sha256=hashlib.sha256(json.dumps(sorted(cells)).encode()).hexdigest())
    return out


def support_table(path):
    table = {}
    for c in summarize(path).values():
        if c['point'] == 'crossing':
            continue
        win = c['windows']
        moved, placed = win.get('moved', []), win.get('placed', [])
        final = moved[-1]
        ev = {}
        for e in final['events']:
            ev.setdefault(e['at'], e)  # first 78d1e0 = layer -1 (ground) or first 66b1b0 sample (flyer)
        src = 'flyer-blend' if 'flyer-blend' in ev else (
            'deck' if final['getter_bridge'] else (
                'water' if ev.get('78bd60', {}).get('present') and abs(final['z'] - (ev['78bd60'].get('water') or 1e9)) < 1e-3 else 'terrain'))
        table[f"{c['type']}@{c['point']}"] = dict(
            z=final['z'], z_bits=final['zbits'], source=src, on_bridge=final['getter_bridge'], deep_flag=final['deep_water'],
            unit280=final['f280_after'], unit1fc='%x' % final['move1fc'], flying5c=final['f5c_fly'],
            first_78d1e0=dict(layer=ev.get('78d1e0', {}).get('layer'), terrain=ev.get('78d1e0', {}).get('terrain'), result=ev.get('78d1e0', {}).get('result')), water=ev.get('78bd60', {}).get('water'),
            jass_locz=c['markers']['placed']['locz'], jass_fly=c['markers']['placed']['fly'],
            layer_m3=ev.get('flyer-blend', {}).get('layer3'), fly=ev.get('flyer-blend', ev.get('nonflyer', {})).get('fly'),
            first_refresh_z=moved[0]['z'] if moved else None, refresh_sequence_z=[s['z'] for s in moved],
            after_nudge_z=placed[-1]['z'] if placed else None, jass_locz_after_nudge=c['markers']['nudged']['locz'])
    return table


def crossing(path):
    steps = [m.split(' label=cross ')[1] for m in markers(path) if ' label=cross ' in m]
    pts = [tuple(float(v) for v in re.search(r'x=(\S+) y=(\S+) locz=(\S+)', s).groups()) for s in steps]
    return dict(steps=len(pts), max_abs_y_offset=max(abs(p[1] - 640.0) for p in pts), max_locz=max(p[2] for p in pts),
                end=pts[-1][:2], path=pts)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--report-dir', type=Path, required=True)
    a = ap.parse_args()
    cap = a.report_dir / 'captures'
    primary = cap / 'observe-authored-v4-1.jsonl'
    repeat = cap / 'observe-authored-v4-2.jsonl'
    blank = cap / 'observe-blank-v4-1.jsonl'
    deck = cap / 'observe-deckwalk-v5-1.jsonl'
    v1 = cap / 'observe-authored-3.jsonl'
    P = rows(primary)
    load = next(r for r in P if r.get('event') == 'map-load-complete')['snapshot']
    start = next(r for r in P if r.get('event') == 'cell-snapshot')['snapshot']
    wpm = (a.report_dir / 'maps' / 'RS-MAP-02.2-authored-v4.wpm').read_bytes()[16:]
    mapping = collections.Counter(('%02x' % wpm[i], '%02x' % (load['words'][i] >> 24)) for i in range(4096))
    B = rows(blank)
    bload = next(r for r in B if r.get('event') == 'map-load-complete')['snapshot']
    exp = dict(task='MAP-02.2', binary_sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',
               captures={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (primary, repeat, blank, deck, v1) if p.exists()},
               lane_bits=dict(zip(LANES, ('02', '04', '08', '10', '20', '40', '80'))),
               loader=dict(authored=dict(wpm_to_top_byte={f'{k[0]}->{k[1]}': v for k, v in sorted(mapping.items())},
                                         after_load=snapshot_digest(load), after_objects=snapshot_digest(start),
                                         top_byte_unchanged_by_objects=[w >> 24 for w in load['words']] == [w >> 24 for w in start['words']],
                                         links_at_load=len(load['linked'])),
                           blank=dict(after_load=snapshot_digest(bload),
                                      all_zero=all(w >> 24 == 0 for w in bload['words']))),
               cells={name: cell_view(start, name) for name in POINTS},
               cells_blank={name: cell_view(next(r for r in B if r.get('event') == 'cell-snapshot')['snapshot'], name) for name in POINTS},
               bridge_objects=dict(LT06_v4=objects(start), LT04_v1=objects(next(r for r in rows(v1) if r.get('event') == 'cell-snapshot')['snapshot'])) if v1.exists() else objects(start),
               support=dict(authored=support_table(primary), blank=support_table(blank)),
               crossing=dict(authored=crossing(primary), blank=crossing(blank)))
    if deck.exists():
        D = rows(deck)
        exp['support']['deckwalk'] = support_table(deck)
        exp['crossing']['deckwalk'] = crossing(deck)
        exp['cells_deckwalk'] = {name: cell_view(next(r for r in D if r.get('event') == 'cell-snapshot')['snapshot'], name) for name in POINTS}
    checks = {}
    if repeat.exists():
        checks['repeat_markers_equal'] = markers(primary) == markers(repeat)
        checks['repeat_support_equal'] = support_table(primary) == support_table(repeat)
        R = rows(repeat)
        checks['repeat_cells_equal'] = snapshot_digest(next(r for r in R if r.get('event') == 'cell-snapshot')['snapshot']) == snapshot_digest(start)
    ctrl = cap / 'control-authored-v4-1-preload.txt'
    if ctrl.exists():
        checks['control_markers_equal_observed'] = preload_markers(ctrl) == preload_markers(cap / 'observe-authored-v4-1-preload.txt')
        checks['control_marker_count'] = len(preload_markers(ctrl))
    exp['checks'] = checks
    out = a.report_dir / 'expected-MAP-02.2.json'
    out.write_text(json.dumps(exp, indent=1, sort_keys=True) + '\n')
    print(hashlib.sha256(out.read_bytes()).hexdigest(), json.dumps(checks))


if __name__ == '__main__':
    main()
