#!/usr/bin/env python3
"""Check ORDER-01.18 Patrol composition claims against the frozen expected JSON, with negative controls.

  order0118_verify.py --expected expected-ORDER-01.18.json [--report out.json]

Claims: issued-Patrol 100-unit expansion threshold (60 / 99.99 / 100), public 851991 through automatic combat,
target loss and leg continuation (internal acquisition sub-chain ahead of the retained d016b leg), queued Move
behind an active leg (runs after the leg's d0175 append), and the queued-Patrol rotation behind a queued non-Patrol
order (5fcc70) with activation-time origin capture.
"""
import argparse, copy, json
from pathlib import Path

W = {'1342.654': 1151849708, '1213.903': 1150794981}


def path(transitions):
    out = []
    for t in transitions:
        if not out or out[-1][1] != t['order']:
            out.append((t['tick'], t['order']))
    return out


def seq(decisions, tick, events=('user-head-dispatch-begin', 'user-append', 'move-dispatch-begin', 'task-prepend')):
    out = []
    for r in decisions:
        if r['tick'] != tick or r['event'] not in events:
            continue
        if r['event'] == 'user-head-dispatch-begin':
            out.append(('HEAD', r['command'], tuple(r['point']), tuple(r['alternate'])))
        elif r['event'] == 'user-append':
            out.append(('APPEND', r['command'], tuple(r['point']), tuple(r['alternate']), r['caller']))
        elif r['event'] == 'move-dispatch-begin':
            out.append(('M', r['code']))
        else:
            out.append(('+', r['code'], r.get('arg')))
    return out


def check(e):
    f = []
    sc = e['scenes']
    for name in ('p2',):
        s = sc[name]
        if not (s['public_identical'] and s['words_identical'] and s['decisions_identical']):
            f.append(f'{name}: repeats/control differ')
    p2 = sc['p2']
    # Threshold (p2 and both p1 runs agree).
    for name in ('p2', 'p1r1', 'p1r2'):
        t = sc[name]['timelines']
        for case, want in (('1', [(0, 0)]), ('3', [(0, 0)]), ('2', [(0, 0), (1, 851991)])):
            if path(t[case]['transitions']) != want:
                f.append(f'{name}/{case}: threshold path {path(t[case]["transitions"])}')
    # Combat composition (p2 case 0).
    t0 = p2['timelines']['0']
    if path(t0['transitions']) != [(0, 0), (1, 851991)]:
        f.append(f'p2/0 path {path(t0["transitions"])}')
    if [(d['tick'], d['source_order']) for d in t0['damage']] != [(27, 851991), (41, 851991), (54, 851991), (68, 851991)]:
        f.append('p2/0 damage')
    d0 = p2['decisions']['0']
    acq = [r['code'] for r in d0 if r['tick'] == 22 and r['event'] == 'task-prepend']
    if acq != ['d0162', 'd0148', 'd016a', 'd016f', 'd0163', 'd0168', 'd0162']:
        f.append(f'p2/0 acquisition chain {acq}')
    if [x for x in seq(d0, 80) if x[0] == 'M'] != [('M', 'd016b')]:
        f.append('p2/0 resume of retained leg at kill tick')
    s106 = seq(d0, 106)
    if not s106 or s106[:2] != [('M', 'd0196'), ('M', 'd0175')] or s106[2][0] != 'APPEND' or s106[2][4] != '6f5fffe8':
        f.append(f'p2/0 continuation at 106 {s106[:3]}')
    # Queued Move behind active leg (p1 run 1 case 4).
    d4 = sc['p1r1']['decisions']['4']
    s83 = seq(d4, 83, ('user-head-dispatch-begin', 'user-append'))
    if [x[:2] for x in s83] != [('APPEND', 851991), ('HEAD', 851986)] or s83[0][4] != '6f5fffe8':
        f.append(f'p1r1/4 queued Move order {s83}')
    # Rotation (p1 run 2 case 4).
    d4 = sc['p1r2']['decisions']['4']
    s83 = [x for x in seq(d4, 83) if x[0] in ('HEAD', 'APPEND', 'M') or (x[0] == '+' and x[1] == 'd0162')]
    want_heads = [x[:2] for x in s83 if x[0] in ('HEAD', 'APPEND')]
    if want_heads != [('APPEND', 851991), ('HEAD', 851990), ('HEAD', 851991), ('APPEND', 851991), ('HEAD', 851986)]:
        f.append(f'p1r2/4 rotation heads {want_heads}')
    else:
        exp = [x for x in s83 if x[0] == 'HEAD' and x[1] == 851991][0]
        rot = [x for x in s83 if x[0] == 'APPEND'][1]
        if exp[2] != (W['1342.654'], W['1213.903']) or rot[2:4] != exp[2:4] or rot[4] != '6f5fe0b8':
            f.append(f'p1r2/4 rotated order {exp} {rot}')
    return f


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--report', type=Path)
    args = ap.parse_args()
    e = json.loads(args.expected.read_text())
    failures = check(e)
    muts = {
        'threshold_99_99_expands': lambda x: x['scenes']['p2']['timelines']['3']['transitions'].insert(2, {'tick': 1, 'label': 'issue', 'order': 851991}),
        'combat_changes_head': lambda x: x['scenes']['p2']['timelines']['0']['damage'][0].__setitem__('source_order', 851983),
        'no_acquisition': lambda x: x['scenes']['p2']['decisions'].__setitem__('0', [r for r in x['scenes']['p2']['decisions']['0'] if r['tick'] != 22]),
        'rotation_missing': lambda x: x['scenes']['p1r2']['decisions'].__setitem__('4', [r for r in x['scenes']['p1r2']['decisions']['4'] if r.get('caller') != '6f5fe0b8']),
        'control_differs': lambda x: x['scenes']['p2'].__setitem__('public_identical', False),
    }
    controls = {}
    for k, m in muts.items():
        c = copy.deepcopy(e)
        m(c)
        controls[k] = 'rejected' if check(c) else 'ACCEPTED'
    ok = not failures and all(v == 'rejected' for v in controls.values())
    rep = {'failures': failures, 'negative_controls': controls, 'status': 'verified' if ok else 'failed'}
    if args.report:
        args.report.write_text(json.dumps(rep, indent=1) + '\n')
    print(json.dumps(rep, indent=1))
    raise SystemExit(0 if ok else 1)


if __name__ == '__main__':
    main()
