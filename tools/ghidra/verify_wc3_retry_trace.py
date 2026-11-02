#!/usr/bin/env python3
"""Replay complete live null-target retries through the production C kernel."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re

HASH='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
MAP='3516ddb2377f3b34d53a76c6b08aafc9a8fcdfce2f0436cdc35697e270d4c027'


def verify(rows,engine):
    meta=rows[0];source=meta.get('source_sha256',{})
    if (meta.get('event')!='metadata' or meta.get('sha256')!=HASH or not meta.get('owned')
            or not meta.get('yieldEvents') or meta.get('map')!=r'Maps\PathingRE-Crowd.w3m'
            or source.get('map')!=MAP):
        raise ValueError('requires owned original crowd retry capture')
    if any(not re.fullmatch('[0-9a-f]{64}',source.get(k,'')) for k in
           ('map','trace_wc3_pathfinding.py','wc3_pathfinding.js')):
        raise ValueError('missing source provenance')
    if any(r.get('event') in ('error','trace-failed') for r in rows):raise ValueError('failed capture')
    ends=[r for r in rows if r.get('event')=='trace-end']
    if len(ends)!=1 or not ends[0].get('installed'):raise ValueError('missing completed observer')
    if not any(r.get('event')=='marker' and 'tick=300 label=complete' in r.get('value','') for r in rows):
        raise ValueError('missing crowd completion')
    normalized=[];counts={}
    for kind,name,n,k in [('retry-init','init',7,3),('retry-result','advance',8,4)]:
        selected=[r for r in rows if r.get('event')==kind];counts[kind]=len(selected)
        if not selected or len(selected)!=ends[0]['counts'].get(kind):raise ValueError('truncated retry calls')
        proc=getattr(engine,'pathing_retry_'+name)
        proc.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)]
        for row in selected:
            for field in ('nativeSource','nativeGoal','ownerBefore','ownerAfter'):
                value=row.get(field)
                if (not isinstance(value,list) or len(value)!=2 or
                        any(type(v)!=int or not 0<=v<=0xffffffff for v in value)):
                    raise ValueError('missing native retry words')
            members=row.get('members')
            if type(members)!=int or not 0<=members<=0xffffffff:raise ValueError('missing registered group count')
            supplied=[*row['nativeSource'],*row['nativeGoal'],members,*row['ownerBefore']]
            if kind=='retry-init':expected=[row['count'],*row['ownerAfter']]
            else:
                target=row.get('target')
                if not isinstance(target,list) or len(target)!=2 or target[0]!=0:
                    raise ValueError('target perimeter outside null-target scope')
                supplied.insert(0,row['before']);expected=[row['result'],row['after'],*row['ownerAfter']]
            out=(ctypes.c_uint32*k)();proc((ctypes.c_uint32*n)(*supplied),out)
            if list(out)!=expected:raise ValueError('retry state differs from production C')
            normalized.append([kind,supplied,expected])
    sequence=hashlib.sha256(json.dumps(normalized,separators=(',',':')).encode()).hexdigest()
    return dict(passed=True,binary_sha256=HASH,source_sha256=source,counts=counts,digest=sequence)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,required=True);parser.add_argument('--engine',type=Path,required=True)
    parser.add_argument('--compare',type=Path);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();engine=ctypes.CDLL(str(args.engine.resolve()))
    read=lambda p:[json.loads(s) for s in p.read_text().splitlines()]
    result=verify(read(args.capture),engine)
    if args.compare:
        if result!=verify(read(args.compare),engine):raise ValueError('repeat retry words/provenance differ')
        result['repeat_exact']=True
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
