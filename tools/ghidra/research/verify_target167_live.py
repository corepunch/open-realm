#!/usr/bin/env python3
"""Verify synchronous hide/cargo TargetLost and subscribed Follow handoffs."""
import argparse
import json
from pathlib import Path
import re

from verify_target166_live import verify as verify_visibility


def scene_rows(rows, scene):
    current = -1
    selected = []
    for row in rows:
        if row['event'] == 'marker':
            match = re.search(r' s=(-?\d+) ', row['value'])
            if match:
                current = int(match[1])
        if current == scene:
            selected.append(row)
    return selected


def public_head(row):
    match = re.search(r' f=[^, ]+,[^, ]+,(\d+)', row['value'])
    if not match:
        raise ValueError('missing public follower head')
    return int(match[1])


def producer_contract(rows):
    contracts = []
    for scene, result in ((8, 0xaa), (9, 0xa9)):
        records = scene_rows(rows, scene)
        handlers = [(i, r) for i, r in enumerate(records) if r['event'] == 'on-target-lost']
        if len(handlers) != 1:
            raise ValueError('missing or duplicate target-loss handler')
        end, handler = handlers[0]
        producers = [(i, r) for i, r in enumerate(records[:end])
                     if r['event'] == 'target-lost' and r['unit'] == handler['t'] and r['c'] == handler['c']]
        if len(producers) != 1:
            raise ValueError('missing synchronous target-loss producer')
        start, producer = producers[0]
        if (producer['caller'] != 0x688373 or producer['a0'] != 0xffffffff or
                producer['a1'] != 0xffffffff or not producer['w20'] & 1 or not producer['w5c'] & 8):
            raise ValueError('world-presence state was not published before loss')
        validation = [r for r in records[start:end] if r['event'] == 'validate' and
                      r['move'] == handler['move'] and r['t'] == handler['t']]
        if len(validation) != 1 or validation[0]['result'] != result or validation[0]['c'] != producer['c']:
            raise ValueError('wrong synchronous target validation')
        if handler['code'] != 0xd01a4 or handler['caller'] != 0x5fdc90:
            raise ValueError('wrong target-loss event dispatch')
        before = [r for r in records[:start] if r['event'] == 'marker' and ' label=sample ' in r['value']]
        after = [r for r in records[end+1:] if r['event'] == 'marker' and ' label=sample ' in r['value']]
        if not before or not after or public_head(before[-1]) != 851971 or any(public_head(r) for r in after):
            raise ValueError('loss or undo changed public cancellation policy')
        if scene == 8:
            begin = [i for i, r in enumerate(records) if r['event'] == 'marker' and 'l=60 label=begin-produce' in r['value']]
            returned = [i for i, r in enumerate(records) if r['event'] == 'marker' and 'l=60 label=end-produce' in r['value']]
            if len(begin) != 1 or len(returned) != 1 or not begin[0] < start < end < returned[0]:
                raise ValueError('ShowUnit returned before target-loss delivery')
            if records[begin[0]]['c'] != producer['c'] or records[returned[0]]['c'] != producer['c']:
                raise ValueError('ShowUnit loss waited for another owner pass')
        tasks = [r for r in records[:start] if r['event'] == 'begin-task' and
                 r['move'] == handler['move'] and r['target'] == handler['t']]
        if len(tasks) != 2 or [r['persistent'] for r in tasks] != [0, 1] or any(r['a3'] != 1 for r in tasks):
            raise ValueError('approach and persistent handoff must both subscribe')
        contracts.append(dict(scene=scene, producer_c=producer['c'], published=[producer['w20'], producer['w5c']],
                              validation=result, event=handler['code'], tasks=[
                                  [r['c'], r['range'], r['persistent'], r['a3']] for r in tasks],
                              before=before[-1]['value'], after=after[0]['value']))
    return contracts


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--visibility', type=Path, required=True)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--header', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    try:
        frozen = json.loads(args.expected.read_text())
        visibility = json.loads(args.visibility.read_text())
        if frozen['binary_sha256'] != visibility['binary_sha256']:
            raise ValueError('target-loss binary provenance differs')
        verified = verify_visibility(visibility, args.archive, args.header)
        repeats = 0
        for name, pin in visibility['captures'].items():
            if pin['family'] != 'loss' or pin['metadata']['mode'] != 'observe':
                continue
            rows = [json.loads(line) for line in (args.archive / name).read_text().splitlines()]
            if producer_contract(rows) != frozen['contracts']:
                raise ValueError('native loss/handoff contract differs from frozen retail')
            repeats += 1
        if repeats != 2:
            raise ValueError('missing producer repeats')
        report = dict(status='live-exact-target-loss', passed=True, captures=verified['captures'],
                      controls=verified['controls'], observed_repeats=repeats, producer_events=4, subscribed_handoffs=4)
    except (ValueError, KeyError, OSError, IndexError) as error:
        report = dict(status='failed', passed=False, error=str(error))
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
