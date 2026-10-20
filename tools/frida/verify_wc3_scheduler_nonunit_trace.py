#!/usr/bin/env python3
"""Verify class15 production and fine searches in a completed retail attack scene."""
import argparse
from collections import Counter, deque
import hashlib
import json
from pathlib import Path
import re

from verify_wc3_scheduler_trace import HASH, LIMIT, RELOAD

SOURCE_HASHES={
    'wc3_pathfinding.js':'d78b04a30fc05b686781273d12b8129403b7149447dfc823b90fc9128e849c74',
    'trace_wc3_pathfinding.py':'b6348d1953da113ec7af30212f15251fd6203bf7935d7b3a97844970d7274514',
    'make_wc3_pathfinding_map.py':'5eeb81376d7ab415a9142b1217572cf9c0877118066de93d5ec07d7a1c2f5e81',
    'wc3_scheduler_nonunit_probe.j':'8991751d17d5f031df08d3e15f22711ac5f6399f9ebe82a6bfd626b118562067',
    'map':'874e7c113e41c1e889ead4121b90aab91beac920776e2d5576c71bd655508291',
}
EVENTS=('scheduler-target','scheduler-class','scheduler-admission','scheduler-unlink','scheduler-update',
        'scheduler-acc-request','scheduler-fine-request','scheduler-nonunit-producer',
        'scheduler-class15-producer','search','fine-result','marker')


def verify(rows):
    metadata=[r for r in rows if r.get('event')=='metadata']
    footer=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or metadata[0].get('sha256')!=HASH or not metadata[0].get('schedulerEvents'):
        raise ValueError('missing pinned non-unit metadata')
    if any(metadata[0].get('source_sha256',{}).get(n)!=h for n,h in SOURCE_HASHES.items()):
        raise ValueError('non-unit producer/observer generation differs')
    if len(footer)!=1 or not footer[0].get('installed') or rows[-1]!=footer[0]:
        raise ValueError('capture is incomplete')
    if any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('observer failed')
    counts=Counter(r.get('event') for r in rows)
    for event in EVENTS:
        if event not in ('search','fine-result','marker') and counts[event]!=footer[0]['counts'].get(event,0):
            raise ValueError('non-unit stream truncated: '+event)
    if counts['search']!=footer[0]['counts'].get('fine-search',0) or counts['fine-result']!=counts['search']:
        raise ValueError('actual fine search/results missing')
    if counts['scheduler-acc-request'] or footer[0]['counts'].get('acc-search',0):
        raise ValueError('this disabled-adaptive producer used a coarse search')
    markers=[r['value'] for r in rows if r.get('event')=='marker']
    if [int(re.search(r'tick=(\d+)',m)[1]) for m in markers if ' label=sample ' in m]!=list(range(1,101)):
        raise ValueError('sample timeline differs')
    if sum('tick=100 label=complete ' in m for m in markers)!=1:
        raise ValueError('completed stopped producer missing')
    names={};stream=[];searches={};classes={};results=deque();transactions=[];routes=[];bridges=set()
    def normalize(x):
        if isinstance(x,dict):return {k:normalize(v) for k,v in x.items() if k not in ('ms','system','fine')}
        if isinstance(x,list):return [normalize(v) for v in x]
        if isinstance(x,str) and x.startswith('0x') and x not in ('0x0','0xffffffff'):
            if x not in names:names[x]=len(names)
            return names[x]
        return x
    for r in rows:
        event=r.get('event')
        if event not in EVENTS:continue
        stream.append(normalize(r))
        if event=='scheduler-nonunit-producer':
            if r['entry']==0x6d3190:
                if r['vtable']!=0xb0fc4c or r['caller']!=0x6d3b77:
                    raise ValueError('CMissileSpiderAttack producer identity differs')
                bridges.add(r['bridge'])
            elif (r['entry'],r['vtable'])!=(0x6cf5e0,0xb7dcc8):
                raise ValueError('unexpected non-unit producer')
        elif event=='scheduler-class15-producer':
            if r['bridge'] not in bridges or (r['caller'],r['value'])!=(0x6d3224,15):
                raise ValueError('class15 bridge producer differs')
        elif event=='scheduler-class':
            expected=(r['before']&0xfdf0ffff)|((r['value']&0xff)<<16)
            if r['after']!=expected:raise ValueError('class setter changed wrong bits')
            classes[r['path']]=r['value']
        elif event=='search':
            if r['kind']!='fine' or classes.get(r['path'])!=15 or (r['budget'],r['footprint'],r['footprintClass'])!=(700,0,0):
                raise ValueError('non-unit quota, class or footprint differs')
            searches.setdefault(r['path'],deque()).append(r)
        elif event=='fine-result':results.append(r)
        elif event=='scheduler-fine-request':
            before,after=r['before'],r['after']
            pending=searches.get(r['path'])
            if r['bucketOffset']!=15*112+3*28 or not r['result'] or not pending or not results:
                raise ValueError('non-unit request did not use class15 fine bucket')
            search=pending.popleft();route=results.popleft()
            if before['work']>1100 or after['work']!=before['work']+search['pops']:
                raise ValueError('non-unit fine charge differs')
            if route['work']!=search['pops'] or route['limit']!=700 or route['radius']!=0 or route['result']!=1:
                raise ValueError('fine route and search differ')
            if after['times']!=[0 if after['work']<64 else before['counter'],before['times'][1]]:
                raise ValueError('non-unit fine timestamp differs')
            transactions.append([before['counter'],15,3,search['pops'],search['budget']])
            routes.append({k:route[k] for k in ('source','goal','radius','limit','words','work','nodes')})
        if event in ('scheduler-admission','scheduler-unlink','scheduler-update','scheduler-fine-request'):
            before=r['stateBefore'] if event=='scheduler-admission' else r['before']
            after=r['stateAfter'] if event=='scheduler-admission' else r['after']
            kind=(r['bucketOffset']%112)//28
            if r['bucketOffset']%28:raise ValueError('unaligned bucket')
            for state in (before,after):
                q=state['queue']
                if state['limit']!=LIMIT[kind] or len(q)!=len(set(q)) or state['head']!=(q[0] if q else '0x0') or state['tail']!=(q[-1] if q else '0x0'):
                    raise ValueError('non-unit budget/FIFO differs')
            if event=='scheduler-admission' and (r['result']!=1 or before['work']>LIMIT[kind]):
                raise ValueError('unexpected admission in this uncontended producer')
            if event=='scheduler-unlink' and (after['work']!=before['work'] or after['queue']!=[p for p in before['queue'] if p!=r['path']]):
                raise ValueError('unlink changed survivors or work')
            if event=='scheduler-update':
                expected=(before['work'],before['countdown']-1) if before['countdown'] else (0,RELOAD[kind])
                if (after['work'],after['countdown'])!=expected:raise ValueError('non-unit reset cadence differs')
    if len(transactions)!=5 or counts['scheduler-class15-producer']!=5 or any(searches.values()) or results:
        raise ValueError('complete five-projectile journey missing')
    if any(r!=routes[0] for r in routes):raise ValueError('repeated projectile routes differ')
    return dict(events=len(stream),transactions=transactions,route=routes[0],
                counts={e:counts[e] for e in EVENTS},stream=stream,
                digest=hashlib.sha256(json.dumps(stream,separators=(',',':')).encode()).hexdigest())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--repeat',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    read=lambda p:[json.loads(l) for l in p.read_text().splitlines()]
    result=verify(read(args.capture));repeat=verify(read(args.repeat))
    if result.pop('stream')!=repeat['stream']:raise ValueError('ordered non-unit repeats differ')
    result.update(repeat_verified=True,capture_sha256=hashlib.sha256(args.capture.read_bytes()).hexdigest(),
                  repeat_sha256=hashlib.sha256(args.repeat.read_bytes()).hexdigest(),source_sha256=SOURCE_HASHES)
    args.output.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
