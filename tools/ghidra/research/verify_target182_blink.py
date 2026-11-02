#!/usr/bin/env python3
"""Verify complete prepared public Blink/Follow repeats and their controls."""
import argparse
import json
from pathlib import Path
from verify_target166_live import verify


def blink_contract(frozen):
    blink = frozen['families']['loss'][10]
    events = [r for r in blink['target_lost'] if r['caller'] == '0x4c9634']
    if len(events) != 1 or int(events[0]['w20'], 16) & 0x800000 == 0:
        raise ValueError('missing reachable Blink transient TargetLost')
    counter = events[0]['c']
    handlers = [r for r in blink['handler'] if r['c'] == counter]
    validations = [r for r in blink['validate'] if r['c'] == counter and r['caller'] == '0x5ff56b']
    if handlers != [dict(c=counter, code='0xd01a4')] or len(validations) != 1:
        raise ValueError('missing synchronous Blink Move receiver')
    if validations[0]['result'] != '0x0' or int(validations[0]['tw'][0], 16) & 0x800000 == 0:
        raise ValueError('Blink validation/window differs')
    for row in blink['public_transitions']:
        if 5 <= row['local'] < 190 and row['follower_order'] != '851971':
            raise ValueError('public Blink Follow was not retained')
    produce = [m for m in blink['markers'] if m['label'] == 'end-produce']
    if len(produce) != 1 or produce[0]['c'] >= counter:
        raise ValueError('missing ordered public issue before later Blink execution')
    if blink['group_hidden']['visits'] != 0:
        raise ValueError('visible Blink witness became a hidden target')
    return dict(blink_events=1, receiver_events=1, retained_groups=len(blink['groups']),
                notification_counter=counter, issue_counter=produce[0]['c'])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--header', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    try:
        frozen = json.loads(args.expected.read_text())
        # Rebuild all summaries from the hash-pinned raw observations, and
        # compare complete public Preload streams with observer-free controls.
        checked = verify(frozen, args.archive, args.header)
        result = dict(status='live-blink-transient-validation', **checked, **blink_contract(frozen))
    except (ValueError, KeyError, OSError, IndexError) as error:
        result = dict(status='failed', passed=False, error=str(error))
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
