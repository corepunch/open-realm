#!/usr/bin/env python3
"""Certify the public twelve-candidate precondition and moving-member layouts."""
import ctypes
import hashlib
import json
import re
from collections import Counter
from pathlib import Path

HASH='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCE_HASHES={
 'make_wc3_pathfinding_map.py':'7d4097220bfaa4b953bb55bb7bf88b8c4a1981306b234443c9d8fa9f798ee6bb',
 'map':'c132959739fa91115d18cd8c4637c4dac78688ad29b834bb20539a73627d2bbd',
 'wc3_formation_boundary_probe.j':'18829685462d3f2d3566853c0cb5a0c6043836b429efdaf3c7308babdde7b7c0',
 'trace_wc3_pathfinding.py':'b403fa54046dbf44b4e4a676cc883e9b5ffd46557ef0b79c0a95f7ed0364c66f',
 'wc3_pathfinding.js':'1abfeb614b99bb84d64acac243b97ea5b2e4c09ac10a6ffc987e38365d2a091a',
}
SIZES=[11,12,13,25]
GOALS=[[1155006464,1153433600],[1132462080,1153433600],
       [1153433600,1153433600],[1153433600,1132462080]]


def verify(rows, engine=None):
    if not rows or rows[0].get('event')!='metadata' or rows[0].get('sha256')!=HASH:
        raise ValueError('missing pinned metadata')
    meta=rows[0]
    if not all(meta.get(k)for k in ('profileEvents','taskEvents','motionEvents','velocityEvents')):
        raise ValueError('incomplete observation configuration')
    if any(meta.get('source_sha256',{}).get(n)!=h for n,h in SOURCE_HASHES.items()):
        raise ValueError('source generation differs')
    if rows[-1].get('event')!='trace-end' or not rows[-1].get('installed') or any(r.get('event')in ('error','trace-failed')for r in rows):
        raise ValueError('incomplete or failed capture')
    counts=Counter(r['event']for r in rows)
    if counts['metadata']!=1 or counts['trace-end']!=1:raise ValueError('duplicate capture')
    for event,count in rows[-1]['counts'].items():
        if event.startswith('formation-') or event in ('motion-decision','velocity-commit'):
            if counts[event]!=count or count>30000:raise ValueError('truncated '+event)
    markers=[r['value']for r in rows if r['event']=='marker']
    samples=[int(re.search(r'tick=(\d+)',v)[1])for v in markers if 'label=sample 'in v]
    if samples!=list(range(1,46)) or [v.split('label=')[1].split()[0]for v in markers if 'label=sample 'not in v]!=['start_formation_boundary','created','complete']:
        raise ValueError('producer boundaries differ')
    setters=[r for r in rows if r['event']=='formation-rank-set']
    if len(setters)!=25 or [r['input']for r in setters]!=[i%4 for i in range(25)]:
        raise ValueError('producer types differ')
    movers=[r['mover']for r in setters]
    if len(set(movers))!=25:raise ValueError('reused producer identity')
    if any(counts[e]!=4 for e in ('group-point-native-begin','group-point-native-end','group-point-request-begin','group-point-request-end')):
        raise ValueError('missing public request boundary')
    if counts['group-point-member-begin']!=94 or counts['group-point-member-end']!=94:
        raise ValueError('incomplete member admission stream')
    if any(r['output']!=1 for r in rows if r['event']=='group-point-member-end'):
        raise ValueError('member callback failed')
    latest={};cases=[];current=None;tick=0
    for row in rows:
        event=row['event']
        if event=='marker' and 'label=sample 'in row['value']:
            tick=int(re.search(r'tick=(\d+)',row['value'])[1])
        elif event=='velocity-commit':latest[row['mover']]=row['after']
        elif event=='group-point-native-begin':
            if current is not None:raise ValueError('nested producer')
            current={'tick':tick,'point':row['point'],'members':[],'candidates':[],'moving':[],'before':dict(latest)}
        elif current is not None and event=='group-point-member-begin':current['members'].append(row)
        elif current is not None and event=='move-request-candidate' and row['override']==0:
            current['candidates'].append(row['mover'])
            pose=current['before'].get(row['mover'])
            current['moving'].append(bool(pose and (pose[4]or pose[5])))
        elif event=='group-point-native-end':
            if current is None or row['accepted']!=1 or row['point']!=current['point']:
                raise ValueError('unmatched or rejected producer')
            cases.append(current);current=None
    if current is not None or len(cases)!=4:raise ValueError('producer count differs')
    moving=[];admitted=[]
    first_units=[]
    for index,case in enumerate(cases):
        n=min(SIZES[index],12);members=case['members']
        if case['tick']!=[9,19,29,39][index] or case['point']!=GOALS[index] or len(members)!=2*n or [r['phase']for r in members]!=['attach']*n+['admit']*n:
            raise ValueError('attachment/admission limit or ordering differs')
        units=[r['unit']for r in members[:n]]
        if len(set(units))!=n or units!=[r['unit']for r in members[n:]]:
            raise ValueError('retained membership differs')
        if index==1:first_units=units
        if index==0:
            if len(units)!=11:raise ValueError('eleven-member boundary missing')
        elif units!=first_units:raise ValueError('prefix was replenished or reordered')
        if any(r['point']!=case['point']for r in members) or case['candidates']!=movers[:n]:
            raise ValueError('point or canonical candidate changed')
        if not any(case['moving']):raise ValueError('moving members not exercised')
        moving.append(sum(case['moving']));admitted.append(n)
    if [r['unit']for r in cases[0]['members'][:11]]!=first_units[:11]:raise ValueError('first prefix differs')
    layouts=[r for r in rows if r['event']=='formation-rank-layout' and len(r['before']['members'])>1]
    if [len(r['before']['members'])for r in layouts]!=admitted:raise ValueError('layout extent differs')
    fixtures=[];words=0
    for index,layout in enumerate(layouts):
        before,after=layout['before'],layout['after'];n=admitted[index]
        if [r['mover']for r in before['members']]!=movers[:n]:raise ValueError('layout membership differs')
        if {k:v for k,v in before.items()if k!='members'}!={k:v for k,v in after.items()if k!='members'}:
            raise ValueError('layout changed owner')
        fixture={'count':SIZES[index],'point':GOALS[index],'heading':layout['heading'],'rows':[]}
        inputs=[n,layout['heading']];expected=[]
        for rank,(a,b)in enumerate(zip(before['members'],after['members'])):
            if len(a['pose'])!=8 or len(a['row'])!=11 or len(b['row'])!=11:
                raise ValueError('truncated member')
            if {k:v for k,v in a.items()if k!='row'}!={k:v for k,v in b.items()if k!='row'} or any(a['row'][k]!=b['row'][k]for k in range(11)if k not in (3,4)):
                raise ValueError('layout changed mover or nonoffset word')
            pose=a['pose'];installed=(a['moverFlags']>>12)&15
            if installed!=rank%4 or any(pose[4:6]):raise ValueError('admission failed to stop/rebind retained mover')
            # Admission has committed the previous movement and stopped velocity.
            inputs.extend([*pose[2:6],*pose[:2],*pose[:2],0x41000000,0x3f780000,installed])
            expected.extend(b['row'][3:5]);fixture['rows'].append([*pose[2:4],installed,*b['row'][3:5]])
        if engine is not None:
            call=engine.pathing_formation_retail;call.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2
            output=(ctypes.c_uint32*(1+2*n))();call((ctypes.c_uint32*len(inputs))(*inputs),output)
            if list(output)!=[1,*expected]:raise ValueError('production scalar layout differs')
        fixtures.append(fixture);words+=len(expected)
    return {'sizes':SIZES,'admitted':admitted,'moving':moving,'layout_words':words,'fixtures':fixtures,'counts':dict(counts),
            'fixture_sha256':hashlib.sha256(json.dumps(fixtures,sort_keys=True,separators=(',',':')).encode()).hexdigest()}
