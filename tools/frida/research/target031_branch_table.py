#!/usr/bin/env python3
"""TARGET-03.1: reachable target-visibility/validation branch table with live witnesses (research tool, new file).

Static rows come from the game.dll 1.27.1.7085 assembly (see the TARGET-03.1 asm excerpts). Each row names a
predicate over the frozen TARGET-03.2 expected files (`target03_expected.py` output); the first matching scene
event becomes the row's live witness. Rows without a witness stay at evidence A (static only).
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path


def vq_steps(v):
    return {n: d for n, d in v['steps']}


def find(exp, pred):
    out = []
    for s in exp['scenes']:
        for w in pred(s):
            out.append(dict(file=exp['_name'], scene=s['scene'], scene_name=s.get('name'), **w))
            break
    return out


def validate(result, caller=None, tw_pred=None):
    def p(s):
        for v in s['validate']:
            if v['result'] == result and (caller is None or v['caller'] == caller) and (tw_pred is None or tw_pred(v['tw'])):
                yield dict(c=v['c'], caller=v['caller'], result=v['result'], target_words=v['tw'])
    return p


def vq(pred):
    def p(s):
        for v in s['visibility_queries']:
            if pred(v, vq_steps(v)):
                yield dict(c=v['first']['c'], role=v['role'], flags=v['flags'], result=v['result'], count=v['count'], steps=v['steps'])
    return p


def lost(caller):
    def p(s):
        for x in s['target_lost']:
            if x['caller'] == caller:
                yield dict(c=x['c'], caller=x['caller'], w20=x['w20'], w5c=x['w5c'])
    return p


def outcome(kind):
    def p(s):
        if s['outcome']['kind'] == kind:
            yield dict(outcome=s['outcome'])
    return p


def episode(pred):
    def p(s):
        for e in s['hidden_episodes']:
            if pred(e):
                yield dict(episode=e)
    return p


def w(tw, i):
    return int(tw[i], 16) if tw else None


ROWS = [
    # CAbilityMove_ValidateTarget 6f5fb940 (stack target widget, RET 4)
    ('V1', '5fb940', 'target null', '0xdd', '5fb969/5fb96d',
     validate('0xdd', tw_pred=lambda tw: tw is None)),
    ('V2', '5fb940', 'CUnit_IsDead (vt+13c, +5c&0x100)', '0xdd', '5fb989..991',
     validate('0xdd', tw_pred=lambda tw: tw and w(tw, 1) & 0x100)),
    ('V3', '5fb940', 'non-unit widget (1d73b0 cast null) with +20&1', '0xaa', '5fb9a6..9b0', None),
    ('V4', '5fb940', 'unit +20&1, not +20&0x800000, +5c&0x10 (loaded cargo)', '0xa9', '5fb9c8..9e4',
     validate('0xa9')),
    ('V5', '5fb940', 'unit +20&1, not +20&0x800000, not +5c&0x10 (RemoveUnit / ShowUnit false)', '0xaa', '5fb9c8..9e4',
     validate('0xaa')),
    ('V6', '5fb940', 'unit +20&0x800000 (set only around morph/blink TargetLost) bypasses the hidden test', 'visibility query', '5fb9d0',
     validate('0x0', tw_pred=lambda tw: tw and w(tw, 0) & 0x800000)),
    ('V7', '5fb940', 'CUnit_IsWidgetVisibleToOwner(mover, target, 0, 4) != 0', '0', '5fb9ec..a06',
     validate('0x0')),
    ('V8a', '5fb940', 'visibility query 0: fogged (detected)', '0xdd', '5fb9ff',
     validate('0xdd', tw_pred=lambda tw: tw and not (w(tw, 1) & 0x01000100))),
    ('V8b', '5fb940', 'visibility query 0: invisible and undetected', '0xdd', '5fb9ff',
     validate('0xdd', tw_pred=lambda tw: tw and w(tw, 1) & 0x01000000)),
    # CUnit_IsWidgetVisibleToOwner 6f66fdd0 (ECX observer, stack target, flags, mode; RET c)
    ('Q1', '66fdd0', 'game d687a8 null or game+3e0 == 0', '0 (not visible)', '66fdd4..de5', None),
    ('Q2', '66fdd0', 'TLS slot 0xd record flag 0x200 set', 'flags |= 1 (skip fog)', '66fdf0..e26',
     vq(lambda v, st: st.get('1dd920', {}).get('flags', 0) & 1)),
    ('Q3', '66fdd0', 'flags 0, target not invisible', 'detect = 1 -> fog test', '1ddff0 5f/1de03c',
     vq(lambda v, st: st.get('1ddff0-detect', {}).get('result') == 1 and v['result'] == 1)),
    ('Q4', '66fdd0', 'invisible target, owner bit in observer player +2e0 mask (own unit or vision shared by the owner)', 'detect = 1',
     '1de004..01f', validate('0x0', tw_pred=lambda tw: tw and w(tw, 1) & 0x01000000)),
    ('Q5', '66fdd0', 'invisible target, CUnit_GetDetectionMask(target,1) & mask', 'detect = 1', '1de027..032', None),
    ('Q6', '66fdd0', 'invisible, undetected', '1dd920 = 0 -> CUnit_IsRevealedToPlayer', '1de034',
     vq(lambda v, st: st.get('1ddff0-detect', {}).get('result') == 0)),
    ('Q7', '66fdd0', 'detected, vision cell (fine>>2) not visible for the observer player (mode 4)', '1dd920 = 0 -> CUnit_IsRevealedToPlayer',
     '1dd9b9 / 1ddee0', vq(lambda v, st: st.get('1ddee0-fog', {}).get('result') == 0)),
    ('Q8', '66fdd0', 'CUnit_IsRevealedToPlayer: (1<<player) & (+148|+14c)', '1', '66fe3b / 699b20',
     vq(lambda v, st: st.get('699b20-mask', {}).get('result') == 1)),
    ('Q9', '66fdd0', 'flags & 1 (Move_CreateTasksFromOrder 5fd35f, 50453f): fog skipped', 'detection only', '1dd97a', None),
    ('Q10', '66fdd0', 'flags & 2 (CAbilityMove vt63 5fa49b): detection skipped', 'fog only', '1dd957', None),
    # Order time
    ('O1', '5fbad0', 'CAbilityMove_CheckTargetOrder: visibility query 0 fails', '0xba (order refused)', '5fbbfd..c10',
     outcome('order-rejected')),
    ('O2', '5fbad0', 'target +20&1 at order time', '0xa9 cargo / 0xaa', '5fbbc0..be1', None),
    ('O3', '5fd270', 'Move_CreateTasksFromOrder 5fd33b: self, dead, hidden, or undetected (flags 1) target dropped -> point task', 'target 0', '5fd33b..368', None),
    ('O4', '5fd270', 'Move_CreateTasksFromOrder 5fd4e2: target fails flags-0 query -> point branch', 'point task', '5fd4e2..4f0', None),
    # Loss events
    ('L1', '651010', 'Unit_RetireWorldPresence (RemoveUnit, ShowUnit false, cargo load)', 'TargetLost', '688373',
     lost('0x688373')),
    ('L2', '651010', 'Unit_BeginDeathTasks', 'TargetLost', '679c2f', lost('0x679c2f')),
    ('L3', '651010', 'CUnit_RefreshInvisibility (+114 != 0 -> +5c|0x01000000)', 'TargetLost', '68b7d8', lost('0x68b7d8')),
    ('L4', '651010', 'CAbilityBlink_MoveCaster (+20|0x800000 around the call)', 'TargetLost', '4c9634', lost('0x4c9634')),
    ('L5', '651010', '69c510 unit event (RemoveUnit second notification)', 'TargetLost', '69c553', lost('0x69c553')),
    ('L6', '651010', 'remaining 16 producers (morph 3f9da6, 48b7f0/48b850/48b890, 4e47e0, 56b4a0, 5fc300, 657920, 658930, 669570, 670070, 678150, CUnit_SetTargetedAs 699860, 69ced0, 6c0890)',
     'TargetLost', 'see Ghidra plate', None),
    ('H1', '5ff490', 'CAbilityMove_OnTargetLost: validate == 0', 'order retained', '5ff56d',
     lambda s: ({'c': x['c'], 'caller': x['caller'], 'result': x['result']} for x in s['validate'] if x['caller'] == '0x5ff56b' and x['result'] == '0x0')),
    ('H2', '5ff490', 'validate != 0, no queued order / internal task 0 or d0173', 'move tasks stopped, order ends', '5ff5d9..64e',
     lambda s: ({'c': x['c'], 'caller': x['caller'], 'result': x['result']} for x in s['validate'] if x['caller'] == '0x5ff56b' and x['result'] != '0x0')),
    ('H3', '5ff490', 'validate != 0 and a queued order with task id not 0/d0173', 'reissue to point (live target position when still visible, else stored point)', '5ff654..6c4', None),
    ('A1', '5fa7b0', 'CAbilityMove_OnArrival: validate != 0', 'order ends at arrival', '5fa80f',
     validate('0xdd', caller='0x5fa814', tw_pred=lambda tw: tw is not None)),
    ('T1', '5ff8b0', 'Move_HandleTargetTask (approach arrival / persistent): validate != 0', 'order ends', '5ff97a..98e',
     validate('0xdd', caller='0x5ff98c')),
    ('G1', '23a760', 'PathGroup_IsTargetNotVisible: member0 query fails', 'unseen+1, cached destination kept, countdown untouched', '23a7e4',
     episode(lambda e: e['visits'] > 0)),
    ('G2', '23a760', 'query succeeds after hidden visits', 'unseen=0; sample when countdown 0', '23a7e4',
     episode(lambda e: e.get('reacquired'))),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('expected', type=Path, nargs='+')
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    exps = []
    for p in a.expected:
        e = json.loads(p.read_text())
        e['_name'] = p.name
        exps.append(e)
    rows = []
    for rid, fn, cond, result, asm, pred in ROWS:
        wit = []
        if pred:
            for e in exps:
                wit += find(e, pred)
        rows.append(dict(id=rid, function='6f' + fn, condition=cond, result=result, asm=asm,
                         evidence='A+L' if wit else 'A', witnesses=wit[:3], witness_scenes=len(wit)))
    doc = dict(task='TARGET-03.1', binary_sha256='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236',
               sources=[dict(file=str(p), sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in a.expected], rows=rows)
    a.output.write_text(json.dumps(doc, indent=1))
    print(a.output, hashlib.sha256(a.output.read_bytes()).hexdigest())
    for r in rows:
        print(r['id'], r['evidence'], r['witness_scenes'], [(x['scene_name'], x.get('c')) for x in r['witnesses']])
    return 0


if __name__ == '__main__':
    sys.exit(main())
