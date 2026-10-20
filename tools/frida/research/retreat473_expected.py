#!/usr/bin/env python3
"""Freeze the GROUP-03.4.7.3 retreat473 public journey from repeated read-only captures.

Inputs: two or more observed captures (JSONL from retreat473_capture.py --mode observe) and
one observer-free control. The observed semantic records (markers, public getter words,
Captain policy calls/snapshots, roster/reissue/placement events) must be identical after
removing process addresses, host milliseconds and sequence numbers, through the final public
marker. Every R473 Preload marker of every run (observed and control) must be identical.
Non-simulation Preload lines (PreloadEnd wall clock, the base map's music choice) are kept
separately and are not compared.

Outputs a JSON fixture and, with --header, the C literal header used by the engine test.
The module functions take in-memory rows/text so the strict verifier can replay its bundle.
"""
import argparse, hashlib, json, re
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
MARK = re.compile(r'call Preload\( "(R473 [^"\r\n]*)" \)')
TRANSPORT = {'metadata', 'module', 'owned-loading-input', 'loading-key', 'trace-end', 'owned-detached', 'preload-file'}
HOME = [0x43c00000, 0x44d00000]  # authored home (384,1664)


def preload_markers(text):
    return MARK.findall(text), [l for l in text.splitlines() if 'Preload' in l and 'R473 ' not in l]


def semantic(rows, name='capture'):
    meta = rows[0]
    if meta.get('event') != 'metadata' or meta.get('mode') != 'observe' or meta.get('sha256') != HASH:
        raise ValueError(f'{name}: not an observed capture of the pinned DLL')
    if not any(r.get('event') == 'preload-file' and r.get('complete') for r in rows):
        raise ValueError(f'{name}: incomplete')
    if any(r.get('event') == 'trace-failed' for r in rows):
        raise ValueError(f'{name}: failed trace')
    ptr = {}

    def alias(v):
        if v in ('0x0', None):
            return v
        return ptr.setdefault(v, f'P{len(ptr)}')
    out = []
    for r in rows:
        if r.get('event') in TRANSPORT:
            continue
        r = {k: v for k, v in r.items() if k not in ('seq', 'ms')}
        for key in ('captain', 'unit', 'target'):
            if isinstance(r.get(key), str):
                r[key] = alias(r[key])
        for key in ('before', 'after'):
            if isinstance(r.get(key), dict):
                r[key] = {**r[key], 'captain': alias(r[key]['captain'])}
        if isinstance(r.get('captain'), dict):
            r['captain'] = {**r['captain'], 'captain': alias(r['captain']['captain'])}
        if 'captains' in r:
            r['captains'] = [{**c, 'captain': alias(c['captain'])} for c in r['captains']]
        out.append(r)
    # The observer keeps recording for a wall-clock tail after the final public marker;
    # the certified interval ends at the last R473 marker (tick300 complete block).
    last = max(i for i, r in enumerate(out) if r.get('event') == 'marker')
    return out[:last + 1]


def freeze(observed, control_rows, preloads, name='retreat473'):
    """observed: list of raw row lists; preloads: list of Preload texts (observed..., control)."""
    runs = [semantic(rows, f'observation{i}') for i, rows in enumerate(observed)]
    if len(runs) < 2:
        raise ValueError('two observations required')
    base = runs[0]
    for i, rows in enumerate(runs[1:], 1):
        if rows != base:
            raise ValueError(f'observation{i}: observed repeat differs')
    if control_rows[0].get('mode') != 'control' or any(r.get('event') in ('module', 'marker') for r in control_rows):
        raise ValueError('control is instrumented')
    markers = [preload_markers(t) for t in preloads]
    ref = markers[0][0]
    if any(m != ref for m, _ in markers):
        raise ValueError('public Preload markers differ')
    if [r['value'] for r in base if r.get('event') == 'marker'] != ref:
        raise ValueError('observer marker stream differs from its Preload file')
    public, pending = [], None
    for r in base:
        if r.get('event') == 'public-position':
            pending = r
        elif r.get('event') == 'marker' and 'label=unit' in r['value']:
            tick, u, order = map(int, re.match(r'R473 tick=(\d+) label=unit u=(\d+) order=(\d+) ', r['value']).groups())
            if pending is None or pending['u'] != u or pending['tick'] != tick:
                raise ValueError('getter/marker pairing')
            public.append([u, tick, order, pending['xy'][0], pending['xy'][1]])
            pending = None
    snaps = []
    for r in base:
        if r.get('event') == 'marker' and 'label=unit' not in r['value']:
            attack = [c for c in r.get('captains', []) if c.get('home') == HOME]
            if not attack:
                continue
            if len(attack) != 1:
                raise ValueError('ambiguous attack Captain')
            c, act = attack[0], attack[0].get('actor') or {}
            snaps.append([r['tick'], r['c'], c['state'], c['flags'], *[v & 0xffffffff for v in c['counts']],
                          *c['request'], c['requestRange'], *act.get('position', [0, 0]), *act.get('velocity', [0, 0])])
    calls = [dict(name=r['name'], tick=r['tick'], c=r['c'], caller=r['caller'], result=r['result'],
                  flags=[r['before']['flags'], r['after']['flags']], counts=r['after']['counts'],
                  request=r['after']['request'], range=r['after']['requestRange'])
             for r in base if r.get('event') == 'captain-call' and r['name'] != 'periodic-update']
    reissues = [dict(tick=r['tick'], c=r['c'], order=r['order'], target=r['target'], point=r['point'], caller=r['caller'])
                for r in base if r.get('event') == 'reissue']
    fixture = dict(markers=len(ref), semantic_records=len(base),
                   semantic_sha256=hashlib.sha256(json.dumps(base, sort_keys=True).encode()).hexdigest(), public_rows=len(public), snapshots=len(snaps),
                   non_simulation_preload_lines=[o for _, o in markers], public=public, captain=snaps,
                   calls=calls, reissues=reissues)
    lines = [f'/* GROUP-03.4.7.3 {name}: frozen retail public getters and attack Captain',
             ' * snapshots (retreat473_expected.py; repeats identical, control markers equal). */',
             f'static uint32_t const {name}_public[][5]={{']
    lines += ['    {' + ','.join(f'0x{v:x}u' for v in row) + '},' for row in public]
    lines += ['};', '/* tick,counter,state,flags,b8..cc,request x/y,range,actor fine x/y,velocity x/y */',
              f'static uint32_t const {name}_captain[][17]={{']
    lines += ['    {' + ','.join(f'0x{v:x}u' for v in row) + '},' for row in snaps]
    lines += ['};', '']
    return fixture, '\n'.join(lines)


def load(path):
    return [json.loads(l) for l in open(path)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--observed', nargs='+', required=True)
    ap.add_argument('--control', required=True, help='observer-free capture JSONL; its -preload.txt is compared')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--header', type=Path)
    ap.add_argument('--name', default='retreat473', help='C table prefix')
    a = ap.parse_args()
    paths = a.observed + [a.control]
    preloads = [Path(str(Path(p).with_suffix('')) + '-preload.txt').read_text(encoding='utf-8', errors='replace') for p in paths]
    fixture, header = freeze([load(p) for p in a.observed], load(a.control), preloads, a.name)
    fixture = dict(task='GROUP-03.4.7.3', scene='retreat473', binary_sha256=HASH, repeats_identical=True,
                   captures=[dict(path=p, sha256=hashlib.sha256(Path(p).read_bytes()).hexdigest()) for p in paths], **fixture)
    a.output.write_text(json.dumps(fixture, indent=1) + '\n')
    if a.header:
        a.header.write_text(header)
    print(json.dumps({k: fixture[k] for k in ('markers', 'semantic_records', 'public_rows', 'snapshots')}))


if __name__ == '__main__':
    main()
