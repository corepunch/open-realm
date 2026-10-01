#!/usr/bin/env python3
"""Replay complete live countdown callers through the production heading math."""
import argparse
from collections import Counter
import ctypes
import hashlib
import json
import re
from pathlib import Path

HASH='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def verify(rows,engine):
    meta=rows[0]
    if (meta.get('event')!='metadata' or meta.get('sha256')!=HASH or not meta.get('owned')
            or not meta.get('yieldEvents') or meta.get('map')!=r'Maps\PathingRE-Crowd.w3m'):
        raise ValueError('requires owned original crowd countdown capture')
    source=meta.get('source_sha256',{})
    if any(not re.fullmatch('[0-9a-f]{64}',source.get(k,'')) for k in
           ('map','trace_wc3_pathfinding.py','wc3_pathfinding.js')):
        raise ValueError('missing source/map provenance')
    if source['map']!='3516ddb2377f3b34d53a76c6b08aafc9a8fcdfce2f0436cdc35697e270d4c027':
        raise ValueError('wrong crowd map')
    if any(r.get('event') in ('error','trace-failed') for r in rows):
        raise ValueError('failed capture')
    ends=[r for r in rows if r.get('event')=='trace-end']
    if len(ends)!=1 or not ends[0].get('installed'):
        raise ValueError('missing completed observer')
    if not any(r.get('event')=='marker' and 'tick=300 label=complete' in r.get('value','') for r in rows):
        raise ValueError('missing completed crowd timeline')
    steps=[r for r in rows if r.get('event')=='waiting-step']
    delays=[r for r in rows if r.get('event')=='path-delay']
    sets=[r for r in rows if r.get('event')=='yield-set']
    counts=ends[0]['counts']
    if (not steps or len(steps)!=counts.get('waiting-step') or len(delays)!=counts.get('path-delay')
            or len(sets)!=counts.get('yield-set') or len(steps)!=len(delays)
            or len(delays)!=sum(r['requested'] for r in sets)):
        raise ValueError('truncated countdown lifecycle')
    if [r['sequence'] for r in steps]!=list(range(1,len(steps)+1)):
        raise ValueError('missing or unordered waiting caller')
    for row in delays:
        if row['disabled'] or not row['before'] or row['after']!=row['before']-1 or row['result']!=1:
            raise ValueError('invalid observed countdown gate')
    for row in sets:
        if (row['requested'] not in (4,20) or row['after']!=max(row['before'],row['requested'])
                or row['stored']!=row['identity']):
            raise ValueError('invalid observed wait writer')
    for name,n in [('pathing_subtract',2),('pathing_heading_error',3)]:
        proc=getattr(engine,name);proc.argtypes=[ctypes.c_uint32]*n;proc.restype=ctypes.c_uint32
    engine.pathing_motion.argtypes=[ctypes.POINTER(ctypes.c_uint32)]
    normalized=[]; gates=Counter()
    for row in steps:
        required=('gate','before','after','arrived','held','changed','source','sourceAfter',
                  'destination','destinationAfter','speed','heading','parameters','nextSpeed','nextHeading',
                  'path','counter')
        if any(k not in row for k in required):
            raise ValueError('missing complete caller operands')
        for name,size in [('source',2),('sourceAfter',2),('destination',2),('destinationAfter',2),('parameters',4)]:
            value=row[name]
            if not isinstance(value,list) or len(value)!=size or any(type(v)!=int or not 0<=v<=0xffffffff for v in value):
                raise ValueError('invalid native caller word vector')
        gate=row.get('gate')
        if (not gate or gate['disabled'] or gate['before']!=row['before'] or not gate['before']
                or gate['after']!=gate['before']-1 or gate['after']!=row['after'] or gate['result']!=1
                or row['arrived'] or row['held'] or row['changed']):
            raise ValueError('caller did not consume an enabled countdown')
        if (row['source']!=row['sourceAfter'] or row['destination']!=row['destinationAfter']
                or gate['source']!=row['source'] or gate['sourceAfter']!=gate['source']
                or gate['destination']!=row['destination'] or gate['destinationAfter']!=gate['destination']):
            raise ValueError('countdown changed the native caller operands')
        x,y=[engine.pathing_subtract(row['destination'][k],row['source'][k]) for k in range(2)]
        error=engine.pathing_heading_error(x,y,row['heading'])
        params=row['parameters']
        motion=(ctypes.c_uint32*7)(row['speed'],row['heading'],error,*params[1:],1)
        engine.pathing_motion(motion)
        if [motion[0],motion[1]]!=[row['nextSpeed'],row['nextHeading']]:
            raise ValueError('complete caller speed/heading differs from production')
        gates[(row['path'],row['counter'],row['before'],row['after'])]+=1
        normalized.append([row['counter'],row['source'],row['destination'],row['speed'],row['heading'],
                           params,row['before'],row['after'],row['nextSpeed'],row['nextHeading']])
    if gates!=Counter((r['path'],r['counter'],r['before'],r['after']) for r in delays):
        raise ValueError('waiting callers do not cover the actual advance events')
    sequence=hashlib.sha256(json.dumps(normalized,separators=(',',':')).encode()).hexdigest()
    return dict(passed=True,binary_sha256=HASH,source_sha256=source,cases=len(steps),digest=sequence)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,required=True);parser.add_argument('--engine',type=Path,required=True)
    parser.add_argument('--compare',type=Path);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();engine=ctypes.CDLL(str(args.engine.resolve()))
    read=lambda p:[json.loads(s) for s in p.read_text().splitlines()]
    result=verify(read(args.capture),engine)
    if args.compare:
        if result!=verify(read(args.compare),engine):
            raise ValueError('repeat caller words/provenance differ')
        result['repeat_exact']=True
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
