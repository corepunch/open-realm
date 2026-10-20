#!/usr/bin/env python3
"""Verify observed ordered blocker decisions and their original owner-call waits."""
import argparse
import copy
import ctypes
import hashlib
import json
import re
from pathlib import Path

HASH='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
EMPTY=[0xffffffff,0xffffffff]


def verify(rows, engine):
    meta=rows[0]
    if meta.get('event')!='metadata' or meta.get('sha256')!=HASH or not meta.get('owned') or not meta.get('yieldEvents'):
        raise ValueError('requires owned, hash-pinned yield input capture')
    source=meta.get('source_sha256',{})
    if any(not re.fullmatch('[0-9a-f]{64}',source.get(k,'')) for k in ('map','trace_wc3_pathfinding.py','wc3_pathfinding.js')):
        raise ValueError('missing source/map provenance')
    if any(r.get('event') in ('error','trace-failed') for r in rows):raise ValueError('failed capture')
    end=next((r for r in reversed(rows) if r.get('event')=='trace-end'),None)
    decisions=[r for r in rows if r.get('event')=='yield-decision']
    if not end or not end.get('installed') or end['counts'].get('yield-decision')!=len(decisions):
        raise ValueError('incomplete decision capture')
    markers=[r['value'] for r in rows if r.get('event')=='marker']
    if not any('tick=300 label=complete' in m for m in markers):raise ValueError('missing completed crowd timeline')
    proc=engine.pathing_yield_decision
    proc.argtypes=[ctypes.POINTER(ctypes.c_uint32)];proc.restype=ctypes.c_uint32
    normalized=[]; assigned={4:0,20:0}; duplicates=0
    for row in decisions:
        current=row['self']; actors={a['path']:a for a in [current,*row['candidates']] if a}
        states={key:copy.deepcopy(actor['before']) for key,actor in actors.items()}
        states[current['path']]['blocker']=EMPTY.copy()
        if current['mover'] not in row['groups']:raise ValueError('missing requester group resolution')
        own_group=row['groups'][current['mover']]; visits={}; inputs=[]
        duplicates+=len([a for a in row['candidates'] if a])-len({a['path'] for a in row['candidates'] if a})
        for peer in row['candidates']:
            if peer is None:continue
            if peer['mover'] not in row['groups']:raise ValueError('missing candidate group resolution')
            group=row['groups'][peer['mover']]
            # A later candidate may be unvisited after an earlier requester wait.
            blocked=False
            probe=[*current['velocity'],current['player'],*peer['velocity'],peer['player'],0,bool(group),False,False]
            if proc((ctypes.c_uint32*10)(*probe)):
                at=visits.get(peer['path'],0); values=row['blocked'].get(peer['path'],[])
                if at>=len(values):raise ValueError('missing ordered blocker resolution')
                blocked=values[at];visits[peer['path']]=at+1
            same=bool(group and own_group and group['pointer']==own_group['pointer'])
            values=[*current['velocity'],current['player'],*peer['velocity'],peer['player'],
                    group['flags'] if group else 0,bool(group),same,blocked]
            choice=proc((ctypes.c_uint32*10)(*values));inputs.append([values,choice])
            if choice==1:
                states[current['path']]={'delay':max(states[current['path']]['delay'],4),'blocker':peer['identity']}
                assigned[4]+=1;break
            if choice==2:
                states[peer['path']]={'delay':max(states[peer['path']]['delay'],20),'blocker':current['identity']}
                assigned[20]+=1
        if any(states[key]!=actor['after'] for key,actor in actors.items()):raise ValueError('production decision state mismatch')
        normalized.append(inputs)
    sets=[r for r in rows if r.get('event')=='yield-set']; delays=[r for r in rows if r.get('event')=='path-delay']
    if len(sets)!=end['counts'].get('yield-set') or len(delays)!=end['counts'].get('path-delay'):
        raise ValueError('truncated wait events')
    for r in sets:
        if r['after']!=max(r['before'],r['requested']) or r['stored']!=r['identity']:raise ValueError('wait writer mismatch')
    for r in delays:
        expected=r['before'] if r['disabled'] else r['before']-1
        if r['after']!=expected or r['result']!=(0x100000 if r['disabled'] else 1):raise ValueError('wait gate mismatch')
    if sum(assigned.values())!=len(sets) or not all(assigned.values()):raise ValueError('missing asymmetric assignments')
    # The fixture retires every assignment and has no prior nonzero wait extensions.
    if len(delays)!=sum(r['requested'] for r in sets):raise ValueError('incomplete wait lifecycle')
    digest=hashlib.sha256(json.dumps(normalized,separators=(',',':')).encode()).hexdigest()
    return dict(passed=True,binary_sha256=HASH,source_sha256=source,decisions=len(decisions),
                assignments=assigned,delay_calls=len(delays),duplicate_candidates=duplicates,digest=digest)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,required=True);parser.add_argument('--engine',type=Path,required=True)
    parser.add_argument('--compare',type=Path);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();engine=ctypes.CDLL(str(args.engine.resolve()))
    result=verify([json.loads(s) for s in args.capture.read_text().splitlines()],engine)
    if args.compare:
        other=verify([json.loads(s) for s in args.compare.read_text().splitlines()],engine)
        if result!=other:raise ValueError('repeat decision/provenance mismatch')
        result['repeat_exact']=True
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
