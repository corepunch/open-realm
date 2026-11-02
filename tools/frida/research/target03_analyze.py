#!/usr/bin/env python3
"""TARGET-03.1/03.2 analyzer: per-scene visibility-policy outcomes from a target03_capture observed run.

Reuses target021_analyze.analyze() for the follower group timeline, then adds per scene:
 * public samples: follower order/position and target visibility per 0.1 s (order transitions, v/h transitions);
 * target-loss chain: 651010 producer call sites, 5ff490 handler entries, 5fb940 validation results with the
   target +20/+5c words, and the 66fdd0 visibility-query branch trace (detection 1ddff0, fog 1ddee0, mask 699b20);
 * group visibility: first hidden visit, maximum unseen counter, reacquisition visit and cached destination;
 * outcome: retained / cancelled (with the deciding decision) / reacquired.
"""
import argparse
import collections
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from target021_analyze import load, analyze as analyze021  # noqa: E402

PRODUCERS = {0x3f9da6: '3f9c10 morph/decouple (sets +20 800000 around the event)', 0x48b843: '48b7f0 ethereal/banish',
             0x48b878: '48b850 teleport/airborne/stun family', 0x48b8c8: '48b890 avatar/stone/defend family',
             0x4c9634: '4c95c0 blink (800000 around the event)', 0x4e4881: '4e47e0', 0x4e48a6: '4e47e0', 0x56b4fd: '56b4a0 inventory',
             0x5fc317: '5fc300 move', 0x657947: '657920', 0x658968: '658930 item hide', 0x669591: '669570 sleep/possession/critter',
             0x6700ac: '670070 build in progress', 0x6781df: '678150 detection change', 0x679c2f: 'Unit_BeginDeathTasks',
             0x688373: 'Unit_RetireWorldPresence (RemoveUnit/ShowUnit false)', 0x68b7d8: '68b780 invisibility transition',
             0x699860: 'CUnit_SetTargetedAs', 0x69c553: '69c510 unit event', 0x69cefa: '69ced0', 0x6c089c: '6c0890 destructable'}


def f32(w):
    return struct.unpack('<f', struct.pack('<I', w & 0xffffffff))[0]


def parse_sample(rest):
    d = dict(re.findall(r'(\w)=(\S+)', rest))
    out = {}
    for k in ('f', 't', 'z'):
        v = d.get(k, 'none')
        out[k] = None if v == 'none' else v.split(',')
    out['v'] = int(d.get('v', -1))
    out['h'] = int(d.get('h', -1))
    return out


def scene_report(s, rows):
    lo, hi = s['window']
    hi = hi if hi is not None else 1 << 62
    inside = [r for r in rows if r.get('c') is not None and lo <= r['c'] <= hi]
    samples = [(m['local'], parse_sample(m['rest'])) for m in s['samples']]
    trans = []
    prev = None
    for l, smp in samples:
        key = (smp['f'][2] if smp['f'] else None, smp['v'], smp['h'], smp['t'][2] if smp['t'] else None)
        if key != prev:
            trans.append(dict(local=l, follower_order=key[0], target_visible=key[1], target_hidden=key[2], target_order=key[3],
                              follower=smp['f'][:2] if smp['f'] else None, target=smp['t'][:2] if smp['t'] else None))
        prev = key
    lost = [dict(c=r['c'], caller=hex(r['caller']), producer=PRODUCERS.get(r['caller'], '?'), w20=hex(r['w20']) if isinstance(r['w20'], int) else r['w20'],
                 w5c=hex(r['w5c']) if isinstance(r['w5c'], int) else r['w5c']) for r in inside if r.get('event') == 'target-lost']
    handler = [dict(c=r['c'], code=hex(r['code']) if isinstance(r['code'], int) else r['code']) for r in inside if r.get('event') == 'on-target-lost']
    validate = [dict(c=r['c'], caller=hex(r['caller']), result=hex(r['result']), tw=[hex(x) for x in r['tw']] if isinstance(r.get('tw'), list) else r.get('tw'))
                for r in inside if r.get('event') == 'validate']
    vq = collections.Counter()
    vq_first = {}
    for r in inside:
        if r.get('event') != 'vis-query':
            continue
        steps = tuple((n, tuple(sorted((k, v) for k, v in d.items() if k in ('flags', 'result', 'detectFlag', 'mode')))) for n, d in r['steps'])
        key = (r['role'], r['flags'], r['mode'], r['result'], steps)
        vq[key] += 1
        vq_first.setdefault(key, dict(c=r['c'], tw=[hex(x) for x in r['tw']] if r.get('tw') else None))
    vis = [dict(role=k[0], flags=k[1], mode=k[2], result=k[3], steps=[[n, dict(d)] for n, d in k[4]], count=n, first=vq_first[k]) for k, n in vq.items()]
    tl = s['timeline']
    hidden = [t for t in tl if any(d['ev'] == 'vis' and d['blocked'] for d in t['decisions'])]
    unseen_max = max((t['unseen'] for t in tl), default=0)
    reacq = None
    for i, t in enumerate(tl):
        if i and tl[i - 1]['unseen'] and t['unseen'] == 0 and t['gi'] == tl[i - 1]['gi']:
            reacq = dict(c=t['c'], prior_unseen=tl[i - 1]['unseen'])
            break
    tasks = [dict(c=r['c'], event=r['event'], caller=hex(r['caller']), range=round(f32(r['range']), 4) if r.get('range') is not None and r['range'] < 0x7f000000 else r.get('range'),
                  persistent=r.get('persistent')) for r in s['tasks'] if r['event'] in ('begin-task',)]
    return dict(scene=s['scene'], window=s['window'], markers=[m for m in s['markers'] if m['label'] not in ('begin-target-move', 'end-target-move')],
                public_transitions=trans, tasks=tasks, groups=[dict(flags=g['flags_first'], visits=g['visits'], c=g['c'], target=g['target']) for g in s['groups']],
                target_lost=lost, handler=handler, validate=validate, visibility_queries=vis,
                group_hidden=dict(first=hidden[0]['c'] if hidden else None, last=hidden[-1]['c'] if hidden else None, visits=len(hidden),
                                  unseen_max=unseen_max, reacquired=reacq),
                counts=s['counts'], violations=len(s['violations']))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('capture', type=Path)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--timeline-report', type=Path)
    a = ap.parse_args()
    rows = load(a.capture)
    base = analyze021(rows)
    rep = dict(capture=str(a.capture), capture_sha256=hashlib.sha256(a.capture.read_bytes()).hexdigest(),
               scenes=[scene_report(s, rows) for s in base['scenes']])
    a.report.write_text(json.dumps(rep, indent=1))
    if a.timeline_report:
        base['capture_sha256'] = rep['capture_sha256']
        a.timeline_report.write_text(json.dumps(base, separators=(',', ':')))
    for s in rep['scenes']:
        print(s['scene'], 'transitions', [(t['local'], t['follower_order'], t['target_visible'], t['target_hidden']) for t in s['public_transitions']][:8])
        print('   lost', [(x['c'], x['producer'][:30]) for x in s['target_lost']], 'validate', [(x['c'], x['caller'], x['result']) for x in s['validate']])
        print('   hidden', s['group_hidden'], 'tasks', [(t['c'], t['caller'], t['range'], t['persistent']) for t in s['tasks']])


if __name__ == '__main__':
    main()
