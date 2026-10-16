#!/usr/bin/env python3
"""Certify fixed-tick formation mutation, retained routes and regroup refresh."""
import ctypes
import hashlib
import json
import re
from collections import Counter
HASH='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCE_HASHES={
 'wc3_formation_refresh_probe.j':'2f15749807cd5d570ad5b97be771304c0db450c86d1fd35fb59a57b6652c18fa',
 'trace_wc3_pathfinding.py':'15f461270ebdc75c67372c1399f9cde10a457ba890d20649c5278123094e23f3',
 'wc3_pathfinding.js':'0bc8e5c9724160e96d395d64ab2a8a5936f5e10973ebf2939b3fc49406e8806a',
 'make_wc3_pathfinding_map.py':'0d34f74c1a85bed0c823825ef2c805cb5b8332e7f94efa8ecbdb312e62cb6012',
 'map':'4863fb6dcd0402f5b8ac394d3881637d66111e15dc3e5bfd584609cf536504ba',
}


def verify(rows,engine=None):
    if not rows or rows[0].get('event')!='metadata' or rows[0].get('sha256')!=HASH:
        raise ValueError('missing pinned metadata')
    meta=rows[0]
    if not all(meta.get(k)for k in ('profileEvents','taskEvents','motionEvents','velocityEvents')):
        raise ValueError('incomplete observation configuration')
    if any(meta.get('source_sha256',{}).get(k)!=v for k,v in SOURCE_HASHES.items()):
        raise ValueError('source generation differs')
    if rows[-1].get('event')!='trace-end' or not rows[-1].get('installed') or any(x.get('event')in ('error','trace-failed')for x in rows):
        raise ValueError('incomplete or failed capture')
    counts=Counter(r['event']for r in rows)
    if counts['metadata']!=1 or counts['trace-end']!=1:raise ValueError('duplicate capture')
    for event,count in rows[-1]['counts'].items():
        if event.startswith('formation-') or event in ('motion-decision','velocity-commit'):
            if count!=counts[event] or count>30000:raise ValueError('truncated '+event)
    samples=[];markers=[];tick=0;timed=[]
    for r in rows:
        if r['event']=='marker':
            match=re.search(r'tick=(\d+) label=(\w+)',r['value'])
            if not match:raise ValueError('bad marker')
            tick=int(match[1]);label=match[2]
            (samples if label=='sample' else markers).append(tick if label=='sample' else [tick,label])
        timed.append((tick,r))
    expected=[[0,'start_formation_refresh'],[0,'created'],[10,'first_order_before'],[10,'first_order_after'],
              [20,'resize_before'],[20,'resize_after'],[30,'remove_before'],[30,'remove_after'],
              [50,'retarget_before'],[50,'retarget_after'],[180,'complete']]
    if markers!=expected or samples!=list(range(1,181)):raise ValueError('producer boundaries differ')
    setters=[r for r in rows if r['event']=='formation-rank-set']
    if [r['input']for r in setters]!=[0,1,2,0,1,2,3]:raise ValueError('resize/rank producer differs')
    movers=[r['mover']for r in setters[:6]]
    if len(set(movers))!=6 or setters[-1]['mover']!=movers[0]:raise ValueError('morph mover identity differs')
    refresh=[r for r in rows if r['event']=='formation-refresh']
    if len(refresh)!=4 or [r['before']['counter']for r in refresh]!=[1058,1092,1191,1456] or [len(r['before']['members'])for r in refresh]!=[6,1,5,5]:
        raise ValueError('refresh producer differs')
    if any(r['before']['clock']!=r['after']['clock'] or r['before']['counter']!=r['after']['counter']for r in refresh):raise ValueError('refresh changed clock')
    first,private,new=[tuple(refresh[i]['before']['identity'])for i in range(3)]
    if len({first,private,new})!=3 or tuple(refresh[3]['before']['identity'])!=new:raise ValueError('physical owner reuse differs')
    membership=[];previous={};routes=[]
    for tick,r in timed:
        if r['event']=='formation-regroup':
            g=r['before'];key=tuple(g['identity']);n=len(g['members'])
            if previous.get(key)!=n:membership.append([tick,g['counter'],list(key),n,g['clock']]);previous[key]=n
        if r['event']=='formation-group-route':
            key=tuple(r['identity']);a,b=r['before'],r['after']
            # A retained route must survive size and membership mutation without
            # generating another layout or changing its point/index/capacity.
            if a['coarseCount']:
                if a!=b or r['formation']!=r['afterFormation']:raise ValueError('cached route changed')
            else:routes.append([list(key),b['coarseCount'],b['coarseIndex'],r['afterFormation']])
    if [x[:4]for x in membership]!=[[10,1058,list(first),6],[20,1092,list(private),1],[20,1092,list(first),5],[30,1125,list(first),4],[50,1191,list(new),5]]:
        raise ValueError('fixed-tick membership history differs')
    if [r[1:3]for r in routes]!=[[10,7],[10,7],[5,2]]:raise ValueError('fresh route history differs')
    resets=[r for r in rows if r['event']=='formation-reset']
    if len(resets)!=1:raise ValueError('reset count differs')
    reset=resets[0];a,b=reset['before'],reset['after']
    if tuple(a['identity'])!=new or [a['counter'],a['age'],a['completion'],a['route']['coarseIndex']]!=[1456,265,0,2] or any(a[k]!=b[k]for k in a if k!='members'):
        raise ValueError('reset observation differs')
    for old,fresh in zip(a['members'],b['members']):
        if any(old['row'][k]!=fresh['row'][k]for k in range(10)) or fresh['row'][10]!=0 or old['pose']!=fresh['pose']:
            raise ValueError('reset erased destination/offset/pose or retained flags')
    advancing=[r for r in rows if r['event']=='formation-regroup' and r['before']['route']['coarseIndex']!=r['after']['route']['coarseIndex']]
    if len(advancing)!=1:raise ValueError('advance count differs')
    advance=advancing[0];a,b=advance['before'],advance['after']
    if a['counter']!=1456 or a['completion']!=0 or b['age']!=0 or b['completion']!=0 or b['route']['coarseIndex']!=0 or b['flags']!=0x10000:
        raise ValueError('regroup advance/counter boundary differs')
    layouts=[r for r in rows if r['event']=='formation-rank-layout']
    if len(layouts)!=4:raise ValueError('layout count differs')
    fixtures=[];words=0
    for layout,renew in zip(layouts,refresh):
        a,b=layout['before'],layout['after'];g=renew['before'];n=len(a['members'])
        if a['identity']!=g['identity'] or a['formation']!=renew['point'] or layout['heading']!=renew['after']['heading']:
            raise ValueError('layout owner/point/heading differs')
        # Read-only geometry radius witness from this same owner visit; creation
        # layout is emitted before the footprint observer, so pair by counter.
        radius={r['mover']:r['radius']for r in rows if r['event']=='mover-radius-state' and r['counter']==g['counter']}
        inputs=[n,layout['heading']];output=[]
        for old,fresh in zip(a['members'],b['members']):
            if len(old['row'])!=11 or len(old['pose'])!=8:raise ValueError('truncated member')
            if any(old['row'][k]!=fresh['row'][k]for k in range(11)if k not in (3,4)) or old['pose']!=fresh['pose']:
                raise ValueError('layout changed nonoffset state')
            pose=old['pose'];rank=(old['moverFlags']>>12)&15
            if old['mover']not in radius:raise ValueError('missing radius witness')
            inputs.extend([*pose[2:6],*pose[:2],*g['clock'][:3],radius[old['mover']],rank])
            output.extend(fresh['row'][3:5])
        if engine is not None:
            call=engine.pathing_formation_retail;call.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2
            result=(ctypes.c_uint32*(1+2*n))();call((ctypes.c_uint32*len(inputs))(*inputs),result)
            if list(result)!=[1,*output]:raise ValueError('production layout differs')
        fixtures.append({'counter':g['counter'],'clock':g['clock'],'point':renew['point'],'inputs':inputs,'offsets':output})
        words+=len(output)
    return {'counts':dict(counts),'membership':membership,'routes':routes,'layout_words':words,'fixtures':fixtures,
            'reset_counter':1456,'advance_age':265,'advance_clock':advance['after']['clock'],
            'fixture_sha256':hashlib.sha256(json.dumps(fixtures,sort_keys=True,separators=(',',':')).encode()).hexdigest()}
