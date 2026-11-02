#!/usr/bin/env python3
"""GROUP-03.4.7.3: freeze/verify the public captain home/retreat contract from analyzed reports.

Heap addresses are replaced by stable names (attack captain `C0`, other captains `C1..`, members `U<n>` in
roster-attach order); host time is dropped.  Everything else (JASS tick, owner counter, callers, results,
state/flag/count words, request/home/actor floats and raw actor words, member fine positions) must be identical
across repeats.  Return values that are not booleans (point-request/go-home leave EAX) are kept only when they are
small; heap-like values become `ptr`.
"""
import argparse
import hashlib
import json
from pathlib import Path

BINARY_SHA256 = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def names(report):
    out, units, caps = {}, 0, 0
    for d in report['decisions']:
        if d['kind'] == 'roster-attach':
            if d['captain'] not in out:
                out[d['captain']] = 'C%d' % caps
                caps += 1
            if d['unit'] not in out:
                out[d['unit']] = 'U%d' % units
                units += 1
    for t in report['timeline']:
        for c in t['captains']:
            if c not in out:
                out[c] = 'C%d' % caps
                caps += 1
    for d in report['decisions']:  # other captains (e.g. defense actor placement at AI start)
        c = d.get('captain')
        if isinstance(c, str) and c not in out:
            out[c] = 'C%d' % caps
            caps += 1
    return out


def scrub(value, table, key=None):
    if isinstance(value, dict):
        return {table.get(k, k): scrub(v, table, k) for k, v in value.items() if k not in ('ms', 'capture')}
    if isinstance(value, list):
        return [scrub(v, table) for v in value]
    if isinstance(value, str):
        return table.get(value, value)
    if isinstance(value, int) and not isinstance(value, bool):
        if hex(value) in table:
            return table[hex(value)]
        if key == 'result' and value > 0xffff and value != 0xffffffff:
            return 'ptr'
    return value


def normalize(report):
    table = names(report)
    return dict(decisions=scrub(report['decisions'], table), timeline=scrub(report['timeline'], table),
                complete=report['complete'], markers=report['footer'].get('markers'))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--report', type=Path, action='append', required=True)
    ap.add_argument('--output', type=Path, help='write frozen expected JSON (first report)')
    args = ap.parse_args()
    states = [normalize(json.loads(p.read_text())) for p in args.report]
    diffs = []
    for i, s in enumerate(states[1:], 1):
        for key in states[0]:
            if s[key] != states[0][key]:
                first = None
                if isinstance(s[key], list):
                    first = next((j for j, (x, y) in enumerate(zip(states[0][key], s[key])) if x != y),
                                 min(len(s[key]), len(states[0][key])))
                diffs.append(dict(repeat=i, key=key, first_index=first,
                                  base=states[0][key][first] if first is not None and first < len(states[0][key]) else None,
                                  other=s[key][first] if first is not None and first < len(s[key]) else None))
    result = dict(reports=[str(p) for p in args.report], identical=not diffs, differences=diffs)
    if args.output:
        reports = [json.loads(p.read_text()) for p in args.report]
        frozen = dict(task='GROUP-03.4.7.3', binary_sha256=BINARY_SHA256,
                      source_reports={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in args.report},
                      captures={r['capture']: r['sha256'] for r in reports}, repeats_identical=not diffs, **states[0])
        args.output.write_text(json.dumps(frozen, indent=1) + '\n')
        result['sha256'] = hashlib.sha256(args.output.read_bytes()).hexdigest()
    print(json.dumps(result, indent=1)[:3000])


if __name__ == '__main__':
    main()
