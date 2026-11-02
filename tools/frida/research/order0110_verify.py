#!/usr/bin/env python3
"""Check the ORDER-01.10 semantic claims against the frozen expected JSON, with negative controls.

  order0110_verify.py --expected expected-ORDER-01.10.json [--report out.json]

Each claim compares the exact public transition list (tick, order) of one case and selected labelled records
(e.g. the order read immediately after KillUnit/RemoveUnit). Negative controls mutate a copy of the expected
data (one order word, one tick, one dropped transition, one identity flag) and must be rejected.
"""
import argparse, copy, json
from pathlib import Path

CLAIMS = {
    ('v3a', '0'): {'path': [(0, 0), (1, 851983), (62, 0)], 'labels': {'kill_after': 851983}},
    ('v3a', '1'): {'path': [(0, 0), (1, 851983), (94, 0)], 'labels': {'kill_after': 851983}},
    ('v3a', '3'): {'path': [(0, 0), (1, 851983), (28, 0)], 'head_point': (1, 851983, [1148977152, 1154121728])},
    ('v3b', '2'): {'path': [(0, 0), (1, 851984), (150, 0)], 'damage': [(35, 851984), (70, 851984), (105, 851984), (140, 851984)]},
    ('v3b', '4'): {'path': [(0, 0), (1, 851983), (25, 851986), (45, 851983), (94, 0)], 'labels': {'reject_attack_invulnerable': 851983, 'kill_after': 851983},
                   'head_point': (12, 851983, [1153138688, 1151418368])},
    ('v3b', '9'): {'path': [(0, 0), (1, 851983), (48, 0)], 'labels': {'remove_after': 851983}},
    ('v3c', '5'): {'path': [(0, 0), (1, 851983), (40, 0)], 'labels': {'death_after': 0, 'replacement': 0}},
    ('v3c', '6'): {'path': [(0, 0)], 'damage': [(19, 0), (32, 0), (46, 0), (59, 0)]},
    ('v3c', '7'): {'path': [(0, 0), (1, 851983), (55, 0)]},
    ('v3d', '8'): {'path': [(0, 0), (1, 851985), (35, 0)], 'first_damage': (29, 851985)},
    ('v3d', '10'): {'path': [(0, 0), (1, 851984), (150, 0)], 'damage': []},
    ('v3d', '11'): {'path': [(0, 0), (1, 851983), (40, 0)], 'first_damage': (28, 851983)},
    ('v3d', '12'): {'path': [(0, 0), (1, 851983), (2, 0), (20, 851986), (22, 851983), (23, 0)], 'head_point': (1, 851983, [1132986368, 1151418368])},
    ('q1', '0'): {'path': [(0, 0), (1, 851986), (50, 851983), (77, 0)]},
    ('q1', '1'): {'path': [(0, 0), (151, 851983), (202, 851986), (225, 0)]},
}


def path(transitions):
    out = []
    for t in transitions:
        if not out or out[-1][1] != t['order']:
            out.append((t['tick'], t['order']))
    return out


def check(expected):
    failures = []
    for (scene, case), claim in CLAIMS.items():
        s = expected['scenes'][scene]
        if not (s['public_identical'] and s['words_identical']):
            failures.append(f'{scene}: repeats/control not identical')
        t = s['timelines'][case]
        p = [x for x in path(t['transitions'])]
        # Labelled records interleave with samples; compare only the order-change path.
        if p != claim['path']:
            failures.append(f'{scene}/{case}: path {p} != {claim["path"]}')
        for label, order in claim.get('labels', {}).items():
            got = [x['order'] for x in t['transitions'] if x['label'] == label]
            if got[:1] != [order]:
                # Unchanged-order labelled records are still emitted (labels are never collapsed).
                failures.append(f'{scene}/{case}: {label} {got} != {order}')
        if 'damage' in claim:
            got = [(d['tick'], d['source_order']) for d in t['damage']]
            if got != claim['damage']:
                failures.append(f'{scene}/{case}: damage {got} != {claim["damage"]}')
        if 'first_damage' in claim:
            got = [(d['tick'], d['source_order']) for d in t['damage']][:1]
            if got != [claim['first_damage']]:
                failures.append(f'{scene}/{case}: first damage {got}')
        if 'head_point' in claim:
            tick, command, point = claim['head_point']
            heads = [r for r in s['decisions'][case] if r['event'] == 'user-head-dispatch-begin' and r['tick'] == tick]
            if not heads or heads[0]['command'] != command or heads[0]['point'] != point:
                failures.append(f'{scene}/{case}: head point {heads[:1]}')
    return failures


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--report', type=Path)
    args = ap.parse_args()
    expected = json.loads(args.expected.read_text())
    failures = check(expected)
    controls = {}
    mutations = {
        'order_word': lambda e: e['scenes']['v3a']['timelines']['0']['transitions'][-1].__setitem__('order', 851983),
        'retire_tick': lambda e: e['scenes']['v3b']['timelines']['9']['transitions'][-1].__setitem__('tick', 45),
        'dropped_transition': lambda e: e['scenes']['v3d']['timelines']['12']['transitions'].pop(2),
        'identity_flag': lambda e: e['scenes']['v3c'].__setitem__('public_identical', False),
        'damage_source': lambda e: e['scenes']['v3d']['timelines']['8']['damage'][0].__setitem__('source_order', 0),
        'head_point': lambda e: e['scenes']['v3a']['decisions']['3'].clear(),
    }
    for name, mutate in mutations.items():
        e = copy.deepcopy(expected)
        mutate(e)
        controls[name] = 'rejected' if check(e) else 'ACCEPTED'
    ok = not failures and all(v == 'rejected' for v in controls.values())
    report = {'claims': len(CLAIMS), 'failures': failures, 'negative_controls': controls, 'status': 'verified' if ok else 'failed'}
    if args.report:
        args.report.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report, indent=1))
    raise SystemExit(0 if ok else 1)


if __name__ == '__main__':
    main()
