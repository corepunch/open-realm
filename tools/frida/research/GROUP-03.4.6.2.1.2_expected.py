#!/usr/bin/env python3
"""GROUP-03.4.6.2.1.2: freeze/verify the public producer contract from analyzed reports.

Normalizes every original 9d86f0 call (address-free: unit key, JASS tick, caller, captain flag
words, unit 5c word, attack words, branch trail, raw range word), the Move task range, the first
physical arrival range per unit and private approach (raw +b0 word), roster callers and suppression
mutations.  All supplied repeats must be identical; the first becomes expected-GROUP-03.4.6.2.1.2.json.
"""
import argparse
import hashlib
import json
from pathlib import Path


def normalize(report):
    calls = []
    for c in report['authored_calls']:
        calls.append(dict(tick=c['tick'], unit=c['unit'], caller=c['caller'], captain_flags=c['captain_flags'],
                          flags5c=c['flags5c'], attack_present=c['attack'] != '0x0', suppress=c['suppress'],
                          weapon_types=c['weapon_types'], targets=c['targets'], ranges=c['ranges'], flags20=c['flags20'],
                          target=c['target'], adjusted=c['adjusted'], trail=c['trail'], range_word=c['range_word'],
                          range=c['range']))
    first = {}
    for a in report.get('arrivals', []):
        key = a['unit']
        if key.startswith('0x'):
            continue
        hist = first.setdefault(key, [])
        if not hist or hist[-1]['stored_word'] != a['stored_word']:
            hist.append(dict(tick=a['tick'], radius=a['radius'], stored_word=a['stored_word'], stored=a['stored']))
    return dict(authored_calls=calls,
                task_ranges=[dict(tick=t['tick'], unit=t['unit'], kind=t['kind'], range=t['range']) for t in report['task_ranges']],
                arrival_ranges={k: v for k, v in sorted(first.items())},
                roster=[dict(tick=r['tick'], name=r['name'], unit=r['unit'], captain_flags=r['captain_flags'], caller=r['caller']) for r in report['roster']],
                reissues=report['reissues'],
                suppression=[dict(tick=s['tick'], release=s['release'], masks=s['masks'], before=s['before'], after=s['after'], caller=s['caller'])
                             for s in report['suppression']],
                markers=[m for m in report['markers'] if ' label=birth ' not in m])


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
                diffs.append(dict(repeat=i, key=key))
    result = dict(reports=[str(p) for p in args.report], identical=not diffs, differences=diffs)
    if args.output:
        frozen = dict(task='GROUP-03.4.6.2.1.2', binary_sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',
                      source_reports={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in args.report},
                      captures={json.loads(p.read_text())['capture']: json.loads(p.read_text())['sha256'] for p in args.report},
                      repeats_identical=not diffs, **states[0])
        args.output.write_text(json.dumps(frozen, indent=1) + '\n')
        result['sha256'] = hashlib.sha256(args.output.read_bytes()).hexdigest()
    print(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
