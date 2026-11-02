#!/usr/bin/env python3
"""Verify complete, read-only retail Smart/Follow scheduler captures."""
import argparse
from collections import Counter, deque
import hashlib
import json
from pathlib import Path
import re

from verify_wc3_scheduler_trace import HASH, LIMIT, RELOAD, SOURCE_HASHES

MAP_HASH='aa9563f48f518f372ead4dd1e299d7193766da4d75f6ad965132db1f23c27ac9'
PRODUCER_HASH='8e960cd4103366eca2fed33cc424177e9fbc5a312691a839475172aeff9866a4'
EVENTS=('scheduler-target','scheduler-class','scheduler-admission','scheduler-unlink',
        'scheduler-update','scheduler-acc-request','scheduler-fine-request','search','marker')


def verify(rows):
    metadata=[r for r in rows if r.get('event')=='metadata']
    footer=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or metadata[0].get('sha256')!=HASH or not metadata[0].get('schedulerEvents'):
        raise ValueError('missing pinned scheduler metadata')
    for name in ('trace_wc3_pathfinding.py','wc3_pathfinding.js'):
        if metadata[0].get('source_sha256',{}).get(name)!=SOURCE_HASHES[name]:
            raise ValueError('observer generation differs')
    if metadata[0].get('source_sha256',{}).get('map')!=MAP_HASH:
        raise ValueError('target producer map differs')
    if len(footer)!=1 or not footer[0].get('installed') or rows[-1]!=footer[0]:
        raise ValueError('capture is incomplete')
    if any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('observer failed')
    counts=Counter(r.get('event') for r in rows)
    for event in EVENTS:
        if event not in ('search','marker') and counts[event]!=footer[0]['counts'].get(event,0):
            raise ValueError('scheduler stream truncated: '+event)
    for kind in ('acc','fine'):
        if sum(r.get('event')=='search' and r.get('kind')==kind for r in rows)!=footer[0]['counts'].get(kind+'-search',0):
            raise ValueError('search stream truncated')
    markers=[r['value'] for r in rows if r.get('event')=='marker']
    samples=[int(re.search(r'tick=(\d+)',m)[1]) for m in markers if ' label=sample ' in m]
    if samples!=list(range(1,301)) or sum('tick=300 label=complete ' in m for m in markers)!=1:
        raise ValueError('complete producer timeline missing')
    if sum('tick=300 label=follow_stop_accepted ' in m for m in markers)!=1:
        raise ValueError('producer did not stop the full journey')
    searches={};admissions={};flags={};groups=[];names={};stream=[]
    def normalize(x):
        if isinstance(x,dict):return {k:normalize(v) for k,v in x.items() if k not in ('ms','system')}
        if isinstance(x,list):return [normalize(v) for v in x]
        if isinstance(x,str) and x.startswith('0x') and x not in ('0x0','0xffffffff'):
            if x not in names:names[x]=len(names)
            return names[x]
        return x
    for r in rows:
        event=r.get('event')
        if event not in EVENTS:continue
        stream.append(normalize(r))
        if event=='search':
            searches.setdefault((r['path'],r['kind']),deque()).append(r)
            continue
        if event=='scheduler-target':
            expected=r['before']
            if bool(expected&0x04000000)!=bool(r['value']):
                expected=(expected&~0x06000000)|(0x04000000 if r['value'] else 0)
            if r['after']!=expected or r['accLimit']!=5000:
                raise ValueError('target policy changed flags or path-owned quota incorrectly')
            flags[r['path']]=r['after'];continue
        if event=='scheduler-class':
            expected=(r['before']&0xfdf0ffff)|((r['value']&0xff)<<16)
            if r['after']!=expected:raise ValueError('class producer changed wrong bits')
            continue
        if event=='marker':continue
        off=r['bucketOffset'];kind=(off%112)//28;player=off//112
        if not 0<=player<16 or off%28:raise ValueError('invalid policy address')
        before=r['stateBefore'] if event=='scheduler-admission' else r['before']
        after=r['stateAfter'] if event=='scheduler-admission' else r['after']
        for s in (before,after):
            q=s['queue']
            if s['limit']!=LIMIT[kind] or len(q)!=len(set(q)) or s['head']!=(q[0] if q else '0x0') or s['tail']!=(q[-1] if q else '0x0'):
                raise ValueError('invalid FIFO or work limit')
        if before['counter']!=after['counter']:raise ValueError('owner changed inside operation')
        if event=='scheduler-admission':
            q=list(before['queue']);path=r['path']
            accepted=before['work']<=LIMIT[kind] and (not q or q[0]==path)
            if r['result']!=int(accepted):raise ValueError('work/FIFO admission differs')
            if accepted and path in q:q.remove(path)
            elif not accepted and path not in q:q.append(path)
            if after['queue']!=q or after['work']!=before['work']:raise ValueError('admission mutated wrong state')
            admissions[path,kind]=r['result']
        elif event=='scheduler-unlink':
            if after['queue']!=[p for p in before['queue'] if p!=r['path']] or after['work']!=before['work']:
                raise ValueError('unlink changed survivors or work')
        elif event=='scheduler-update':
            expected=(before['work'],before['countdown']-1) if before['countdown'] else (0,RELOAD[kind])
            if (after['work'],after['countdown'])!=expected or after['queue']!=before['queue']:
                raise ValueError('reset cadence differs')
        else:
            if r['result']!=1 or admissions.pop((r['path'],kind),None)!=1:
                raise ValueError('target fixture request lacked admission')
            pending=searches.get((r['path'],'fine' if kind==3 else 'acc'))
            if not pending:raise ValueError('request lacks actual search')
            search=pending.popleft()
            if search['budget']!=(700 if kind==3 else 400 if kind==2 else 5000):
                raise ValueError('bucket selector replaced path-owned search quota')
            charge=search['pops']
            if after['work']!=before['work']+charge:raise ValueError('search charge differs')
            times=list(before['times']);slot=0 if kind==3 else 1
            times[slot]=0 if (after['work']<64 if kind==3 else charge<32) else before['counter']
            if after['times']!=times:raise ValueError('request timestamp differs')
            if kind in (0,1):
                target=bool(flags.get(r['path'],0)&0x04000000)
                if kind!=int(target):raise ValueError('group uses wrong target policy')
                groups.append([before['counter'],kind,charge,search['budget']])
    if any(searches.values()):raise ValueError('unconsumed search work')
    if counts['scheduler-target']!=1015 or len(groups)!=6 or {g[1] for g in groups}!={0,1}:
        raise ValueError('full target/point route sequence missing')
    return dict(events=len(stream),groups=groups,counts={e:counts[e] for e in EVENTS},
                digest=hashlib.sha256(json.dumps(stream,separators=(',',':')).encode()).hexdigest(),stream=stream)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--repeat',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    read=lambda p:[json.loads(l) for l in p.read_text().splitlines()]
    result=verify(read(args.capture));repeat=verify(read(args.repeat))
    if result.pop('stream')!=repeat['stream']:raise ValueError('ordered target scheduler repeats differ')
    result.update(repeat_verified=True,capture_sha256=hashlib.sha256(args.capture.read_bytes()).hexdigest(),
                  repeat_sha256=hashlib.sha256(args.repeat.read_bytes()).hexdigest(),map_sha256=MAP_HASH,
                  producer_sha256=PRODUCER_HASH)
    args.output.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
