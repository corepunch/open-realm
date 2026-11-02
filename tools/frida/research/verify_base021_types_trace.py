#!/usr/bin/env python3
"""BASE-02.1 strict checker for base021_trace.py captures.

Rejects incomplete captures, normalizes unit pointers to creation indices, checks every
authored birth publication against the original-code oracle report, and (optionally)
compares two captures' complete normalized producer/publication/support sequences.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

PROBE_TYPES = ['hM00', 'hM01', 'hM02', 'hM03', 'hM04', 'hM05', 'hM06', 'hM07', 'hM08', 'hM09', 'hM10', 'hM11',
               'hM12', 'hM13', 'hM14', 'hfoo', 'hfoo', 'hsor', 'hfoo', 'edot', 'orai', 'hgry', 'ucry', 'hgry']
UMVT = {'hM00': 'foot', 'hM01': 'horse', 'hM02': 'fly', 'hM03': 'hover', 'hM04': 'float', 'hM05': 'amph',
        'hM06': 'unbuild', 'hM07': 'none', 'hM08': '', 'hM09': '_', 'hM10': 'FLY', 'hM11': 'Float',
        'hM12': 'foot,fly', 'hM13': 'boat', 'hM14': '-'}
STOCK = {'hfoo': 'foot', 'hsor': 'hover', 'edot': 'foot', 'orai': 'horse', 'hgry': 'fly', 'ucry': 'foot'}


def raw(code):
    return bytes.fromhex(code).decode('latin1')


def load(path):
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    end = [r for r in rows if r.get('event') == 'trace-end']
    if len(end) != 1 or any(r.get('event') == 'trace-failed' or r.get('type') == 'error' for r in rows):
        raise SystemExit(f'{path}: incomplete or failed capture')
    markers = [r['value'] for r in rows if r.get('event') == 'marker']
    samples = [m for m in markers if ' label=sample ' in m]
    if len(samples) != 300 or not markers[-1].startswith('PATHTRACE tick=300 label=complete'):
        raise SystemExit(f'{path}: missing samples/completion')
    return rows, end[0]


def normalize(rows, probe_types=None):
    probe_types = PROBE_TYPES if probe_types is None else probe_types
    births = [r for r in rows if r.get('event') == 'publish-class' and r['caller'] == '68a4f3']
    if len(births) != len(probe_types):
        raise SystemExit('unexpected birth count %d' % len(births))
    index = {r['unit']: i for i, r in enumerate(births)}
    for i, r in enumerate(births):
        if raw(r['rawcode'])[::-1] != probe_types[i] and raw(r['rawcode']) != probe_types[i]:
            raise SystemExit(f'birth {i} rawcode {r["rawcode"]}')
    out = dict(birth=[], events=[], support={}, parse=[])
    start = next(n for n, r in enumerate(rows) if r.get('event') == 'marker')
    for i, r in enumerate(births):
        u = r['unit']
        pubs = [p for p in rows[:start] if p.get('event') == 'publish-profile' and p['unit'] == u and p['caller'] == '694602']
        adaptive = [p for p in rows[:start] if p.get('event') == 'set-adaptive' and p['unit'] == u and p['caller'] == '68a169']
        out['birth'].append(dict(index=i, type=probe_types[i], class_flags=r['args'][0],
                                 profile=pubs[0]['args'] if pubs else None, profile_count=len(pubs),
                                 adaptive=adaptive[0]['args'][0] if adaptive else None))
    seen_birth = set()
    for r in rows:
        e = r.get('event')
        if e in ('publish-profile', 'publish-class', 'set-adaptive', 'set-category', 'set-query'):
            if r['unit'] not in index:
                continue
            key = (e, r['unit'])
            if r['caller'] in ('68a4f3', '68a169') or (r['caller'] == '694602' and key not in seen_birth and False):
                continue
            out['events'].append([e, index[r['unit']], raw(r['rawcode']), r['args'], r['caller']])
        elif e == 'producer':
            b, a = r.get('before', {}), r.get('after', {})
            u = b.get('unit')
            out['events'].append(['producer', r['name'], index.get(u), r['caller'],
                                  {k: r[k] for k in ('newRawcode', 'normal', 'alt', 'edx', 'stack', 'height', 'floor', 'raise', 'rate') if k in r},
                                  [b.get(k) for k in ('rawcode', 'move1fc', 'ground200', 'targ24c', 'flags20', 'flags5c', 'fly208', 'floor20c', 'max210')],
                                  [a.get(k) for k in ('rawcode', 'move1fc', 'ground200', 'targ24c', 'flags20', 'flags5c', 'fly208', 'floor20c', 'max210')]])
        elif e == 'support' and r['unit'] in index:
            out['support'].setdefault(index[r['unit']], []).append([r['xyz'], r['support280'], r['move1fc'], r['flags5c'], r['ground200']])
        elif e == 'movetp-parse':
            out['parse'].append([r['input'], r['bits']])
        elif e in ('stock-marker', 'marker'):
            out['events'].append([e, r['value']])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('capture', type=Path)
    ap.add_argument('--oracle', type=Path, required=True, help='verify_base021_movement_types.py report')
    ap.add_argument('--compare', type=Path)
    ap.add_argument('--report', type=Path, required=True)
    args = ap.parse_args()
    oracle = json.loads(args.oracle.read_text())
    parse = {p['text']: p['bits'] for p in oracle['parse']}
    lanes = {l['bits']: l for l in oracle['lanes']}
    rows, end = load(args.capture)
    norm = normalize(rows)
    failures = []
    report_births = []
    for b in norm['birth']:
        text = UMVT.get(b['type'], STOCK.get(b['type']))
        bits = parse[text]
        lane = lanes[bits]
        want = dict(class_flags=lane['class_flags'], profile=[lane['map_edx_1'], lane['map_edx_0']], adaptive=1, profile_count=2)
        got = dict(class_flags=b['class_flags'], profile=b['profile'], adaptive=b['adaptive'], profile_count=b['profile_count'])
        b = dict(b, movetp=text, bits=bits)
        report_births.append(b)
        if got != want:
            failures.append(dict(index=b['index'], type=b['type'], want=want, got=got))
    # Every live parse matches the original parser oracle when the string is in the oracle set.
    parse_mismatch = [p for p in norm['parse'] if bytes.fromhex(p[0]).decode('latin1') in parse and parse[bytes.fromhex(p[0]).decode('latin1')] != p[1]]
    report = dict(capture=str(args.capture), capture_sha256=hashlib.sha256(args.capture.read_bytes()).hexdigest(),
                  counts=end['counts'], births=report_births, birth_failures=failures,
                  parse_rows=len(norm['parse']), parse_mismatch=parse_mismatch,
                  producer_events=sum(1 for e in norm['events'] if e[0] == 'producer'),
                  support_units=len(norm['support']))
    if args.compare:
        rows2, _ = load(args.compare)
        norm2 = normalize(rows2)
        diffs = {}
        for key in ('birth', 'events', 'support', 'parse'):
            a, b = norm[key], norm2[key]
            if key == 'support':
                # 684480 runs per presentation frame; emitted change counts depend on frame timing.
                # Compare frame-independent witnesses: first/last output and the ordered distinct state words.
                def reduce(m):
                    out = {}
                    for k, v in m.items():
                        states = []
                        for row in v:
                            st = row[1:]
                            if not states or states[-1] != st:
                                states.append(st)
                        out[str(k)] = dict(first=v[0], last=v[-1], states=states)
                    return out
                a, b = reduce(a), reduce(b)
            if a != b:
                if isinstance(a, list):
                    first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
                    diffs[key] = dict(len=[len(a), len(b)], first_index=first,
                                      first=[a[first] if first < len(a) else None, b[first] if first < len(b) else None])
                else:
                    keys = sorted(set(a) | set(b), key=int)
                    diffs[key] = {k: dict(len=[len(a.get(k, [])), len(b.get(k, []))]) for k in keys if a.get(k) != b.get(k)}
        report.update(compare=str(args.compare), compare_sha256=hashlib.sha256(args.compare.read_bytes()).hexdigest(),
                      identical=not diffs, differences=diffs)
    args.report.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({k: report[k] for k in report if k not in ('births',)}, indent=1)[:4000])
    if failures or parse_mismatch:
        sys.exit(1)


if __name__ == '__main__':
    main()
