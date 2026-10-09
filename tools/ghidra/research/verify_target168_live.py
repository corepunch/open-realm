#!/usr/bin/env python3
"""Check delayed retail invisibility publication and three Follow policies."""
import argparse
import json
from pathlib import Path

from verify_target166_live import verify as verify_visibility
from verify_target167_live import scene_rows, public_head


def invisibility_contract(rows):
    contracts = []
    for scene, result, head in ((3, 0xdd, 0), (4, 0, 851971), (5, 0, 851971)):
        records = scene_rows(rows, scene)
        losses = [(i, r) for i, r in enumerate(records)
                  if r['event'] == 'target-lost' and r['caller'] == 0x68b7d8]
        if len(losses) != 1:
            raise ValueError('missing/duplicate invisibility publisher')
        start, loss = losses[0]
        if (not loss['w5c'] & 0x01000000 or loss['w20'] & 1 or
                loss['a0'] != 0xffffffff or loss['a1'] != 0xffffffff):
            raise ValueError('invisibility state not published before TargetLost')
        produced = [r for r in records[:start] if r['event'] == 'marker' and
                    'l=60 label=end-produce' in r['value']]
        if len(produced) != 1 or loss['c'] <= produced[0]['c']:
            raise ValueError('authored fade did not defer publication')
        handlers = [(i, r) for i, r in enumerate(records[start:], start)
                    if r['event'] == 'on-target-lost' and r['t'] == loss['unit'] and r['c'] == loss['c']]
        if len(handlers) != 1:
            raise ValueError('missing/duplicate Follow handler')
        end, handler = handlers[0]
        if handler['code'] != 0xd01a4 or handler['caller'] != 0x5fdc90:
            raise ValueError('wrong target-loss event')
        validations = [r for r in records[start:end] if r['event'] == 'validate' and
                       r['t'] == loss['unit'] and r['move'] == handler['move']]
        if len(validations) != 1 or validations[0]['result'] != result or validations[0]['c'] != loss['c']:
            raise ValueError('wrong synchronous Follow validation')
        stops = [i for i, r in enumerate(records[end+1:], end+1)
                 if r['event'] == 'marker' and 'label=begin-stop' in r['value']]
        if len(stops) != 1:
            raise ValueError('missing/duplicate explicit Stop boundary')
        stop = stops[0]
        after = [r for r in records[end+1:stop] if r['event'] == 'marker' and 'label=sample' in r['value']]
        if not after or any(public_head(r) != head for r in after):
            raise ValueError('wrong retained/canceled head or automatic reacquisition')
        stopped = [r for r in records[stop+1:] if r['event'] == 'marker' and 'label=sample' in r['value']]
        if not stopped or any(public_head(r) for r in stopped):
            raise ValueError('explicit Stop failed to clear retained Follow')
        contracts.append(dict(scene=scene, begin_c=produced[0]['c'], loss_c=loss['c'],
                              published=[loss['w20'], loss['w5c']], validation=result,
                              event=handler['code'], after_head=public_head(after[0])))
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
            raise ValueError('invisibility binary provenance differs')
        prerequisite = verify_visibility(visibility, args.archive, args.header)
        repeats = 0
        for name, pin in visibility['captures'].items():
            if pin['family'] != 'loss' or pin['metadata']['mode'] != 'observe':
                continue
            rows = [json.loads(line) for line in (args.archive/name).read_text().splitlines()]
            if invisibility_contract(rows) != frozen['contracts']:
                raise ValueError('raw invisibility contract differs from frozen retail')
            repeats += 1
        if repeats != 2:
            raise ValueError('missing invisibility repeats')
        report = dict(status='live-exact-invisibility-loss', passed=True, captures=prerequisite['captures'],
                      controls=prerequisite['controls'], observed_repeats=repeats, policy_events=repeats*3)
    except (ValueError, KeyError, OSError, IndexError) as error:
        report = dict(status='failed', passed=False, error=str(error))
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
