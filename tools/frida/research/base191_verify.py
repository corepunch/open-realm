#!/usr/bin/env python3
"""Verify complete JASS/AI nesting and canonical arrival publication, with control."""
import argparse
import hashlib
import json
from pathlib import Path
from follow187_engine_fixture import capture

EVENTS={'jass-begin','jass-end','point-task-begin','point-task-end','captain-begin','captain-end','bridge','bridge-end','range-publish'}


def timeline(rows):
    stack=[];begins={};publications={};stream=[]
    for r in rows:
        e=r['event']
        if e not in EVENTS:continue
        stream.append(r)
        if e.endswith('-begin')or e=='bridge':
            if r['id']in begins or r['parent']!=(stack[-1]['id']if stack else 0):raise ValueError('publisher nesting differs')
            stack.append(r);begins[r['id']]=r
        elif e=='range-publish':
            if not stack or stack[-1]['event']!='bridge'or r['bridge']!=stack[-1]['id']:raise ValueError('range escaped bridge')
            if r['bridge']in publications or r['caller']!=0x05bb60:raise ValueError('range producer differs')
            publications[r['bridge']]=r['value']
        else:
            if not stack or stack[-1]['id']!=r['id']:raise ValueError('unbalanced publisher return')
            start=stack.pop()
            if e=='jass-end'and r['result']!=1:raise ValueError('public native rejected')
    if stack:raise ValueError('incomplete publisher')
    jass=[r for r in begins.values()if r['event']=='jass-begin']
    bridges=[r for r in begins.values()if r['event']=='bridge']
    if len(jass)!=2 or len(bridges)!=5 or len(publications)!=5:raise ValueError('incomplete entry coverage')
    if [r['order']for r in jass]!=[851986]*2:raise ValueError('public Move order differs')
    ordinary=[r for r in bridges if r['producer']=='point-task'];ai=[r for r in bridges if r['producer']=='captain']
    if len(ordinary)!=2 or len(ai)!=3:raise ValueError('publisher families absent')
    for r in bridges:
        if r['events']!=[0xd0196,0xd0198]or r['flag']!=1:raise ValueError('common bridge flags/events differ')
        parent=begins[r['parent']]
        if r['producer']=='point-task':
            root=begins[parent['parent']]
            if root['event']!='jass-begin'or r['point']!=root['point']or r['caller']!=0x5ffdb4:raise ValueError('JASS did not reach task bridge')
            if r['range']!=0 or publications[r['id']]!=0x3efae148:raise ValueError('zero world range not normalized')
        else:
            if r['caller']!=0x9d4580 or not r.get('actorBridge')or r['point']!=parent['point']or r['range']!=parent['range']:raise ValueError('Captain virtual bridge differs')
    if [r['range']for r in ai]!=[0x43fa0000,0x43480000,0x43480000]:raise ValueError('home/point AI ranges differ')
    if [publications[r['id']]for r in ai]!=[0x417a0000,0x40c80000,0x40c80000]:raise ValueError('AI fine publication differs')
    return stream


def verify(expected,archive):
    observed=[];public=[]
    for name,pin in expected['captures'].items():
        rows,markers,mode=capture(archive/name,pin,expected['binary_sha256'],'PATHTRACE ',307);public.append(markers)
        if mode=='observe':observed.append(timeline(rows))
    if len(observed)!=2 or observed[0]!=observed[1]:raise ValueError('complete entry streams differ')
    if len(public)!=3 or any(p!=public[0]for p in public):raise ValueError('observer perturbed public timeline')
    normalized=json.dumps(observed[0],separators=(',',':')).encode()
    if hashlib.sha256(normalized).hexdigest()!=expected['timeline_sha256']:raise ValueError('frozen entry stream differs')
    return dict(passed=True,status='live-common-point-entries',jass_entries=2,ai_entries=3,bridge_publications=5,public_markers=307,observations=2,controls=1)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('expected','archive','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();result=verify(json.loads(a.expected.read_text()),a.archive)
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
