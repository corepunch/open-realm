#!/usr/bin/env python3
"""Project whole actual point-command lifetimes without discarding scalar state."""
import json
import ctypes
from pathlib import Path

SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
EVENTS={'marker','point-task','point-request','range-publication','attack-command',
        'arrival','velocity','clear-force','stop','force-range','move-cantpath',
        'move-arrival','turn-or-stop'}
GOALS={
    "exact":[0x44c00000]*4+[0x4403eb84,0x4403eb85,0x4403eb86,0x44000000],
    "terrain":[0x44c00000,0x4403eb82,0x4403eb83,0x4403eb84]}
MARKERS={"exact":754,"terrain":378}


def arithmetic(rows, engine):
    """Compare every observed arrival and velocity call with production helpers."""
    pointer=ctypes.POINTER(ctypes.c_uint32)
    engine.pathing_arrival.argtypes=[pointer,pointer]
    engine.pathing_velocity_commit.argtypes=[pointer]
    arrivals=velocities=0
    for row in rows:
        if row.get('event')=='arrival':
            words=(ctypes.c_uint32*7)(*row['source'],*row['destination'],
                row['heading'],row['threshold'],row['before']['flags'])
            output=(ctypes.c_uint32*4)()
            engine.pathing_arrival(words,output)
            assert list(output)[1:]==[row['angle'],row['inside'],row['result']],'arrival decision differs'
            arrivals+=1
        elif row.get('event')=='velocity':
            pose=row['before']['pose']
            words=(ctypes.c_uint32*6)(pose[4],pose[5],row['speed'],row['heading'],pose[6],pose[7])
            engine.pathing_velocity_commit(words)
            after=row['after']['pose']
            assert [words[0],words[1],words[5]]==[after[4],after[5],after[7]],'velocity commit differs'
            velocities+=1
    assert arrivals==velocities and arrivals,'missing complete arithmetic lifetime'
    return dict(arrivals=arrivals,velocities=velocities,mismatches=0)


def load(path,variant="exact"):
    rows=[json.loads(line)for line in Path(path).read_text().splitlines()]
    validate(rows,variant)
    return rows


def validate(rows,variant="exact"):
    assert rows and rows[0].get('event')=='metadata','missing provenance'
    meta=rows[0]
    assert meta.get('task')=='TARGET-01.2-payoff371' and meta.get('owned') and meta.get('sha256')==SHA,'wrong owned image/task'
    assert not any(row.get('type')=='error' or row.get('event')in('trace-failed','script-error')for row in rows),'failed capture'
    assert rows[-1].get('event')=='preload-file' and rows[-1].get('complete') and rows[-1].get('markers')==MARKERS[variant],'incomplete public capture'
    if meta['mode']=='observe':
        ending=[row for row in rows if row.get('event')=='trace-end']
        assert len(ending)==1 and ending[0].get('installed'),'missing observer completion'
        counts=ending[0]['counts']
        for kind in EVENTS|{'module'}:
            assert counts.get(kind,0)==sum(row.get('event')==kind for row in rows),'truncated '+kind
        markers=[row['value']for row in rows if row.get('event')=='marker']
        assert len(markers)==MARKERS[variant] and 'label=complete' in markers[-1],'incomplete observer producer'
    else:
        assert meta['mode']=='control' and not any(row.get('event')in EVENTS|{'module','trace-end'}for row in rows),'control contains hooks'
    return rows


def project(rows,variant="exact"):
    validate(rows,variant)
    assert rows[0]['mode']=='observe','observed state required'
    tasks=[row for row in rows if row.get('event')=='point-task']
    requests=[row for row in rows if row.get('event')=='point-request']
    ranges=[row for row in rows if row.get('event')=='range-publication']
    assert len(tasks)==len(requests)==len(ranges)==len(GOALS[variant]),'missing actual physical request'
    for scene,(task,request,publication)in enumerate(zip(tasks,requests,ranges)):
        assert task['scene']==request['scene']==publication['scene']==scene,'request producer scene order'
        assert task['code']==0xd016b and task['range']==request['range']==0,'actual zero-range task'
        assert task['point']==request['point']==[GOALS[variant][scene],0x44800000],'public click changed or non-adjacent boundary'
        assert request['flag']==1 and request['arrival']==0xd0196 and request['cantpath']==0xd0198,'completion contract'
        assert publication['value']==publication['after']['range']==0x3efae148,'canonical point range'
        before=next(i for i,r in enumerate(rows)if r.get('event')=='marker' and r.get('scene')==scene and 'label=issue-before' in r['value'])
        after=next(i for i,r in enumerate(rows)if r.get('event')=='marker' and r.get('scene')==scene and 'label=issue-after' in r['value'])
        assert before<rows.index(task)<rows.index(request)<rows.index(publication)<after,'physical admission not synchronous'
    forced=[row for row in rows if row.get('event')=='force-range']
    if variant=='exact':assert [r['scene']for r in forced]==[2,3,4,5],'occupied and near-boundary force producers'
    else:assert [r['scene']for r in forced]==[0,3],'static blocked goal and actual outer boundary force producers'
    assert all(r['after']['flags']==r['before']['flags']|0x10000 for r in forced),'force bit publication'
    assert [r['scene']for r in rows if r.get('event')=='move-cantpath']==[r['scene']for r in forced],'cannot-path delivery'
    # Every scene explicitly removes both prior units before the next birth.
    # Address reuse across disjoint lifetimes is not an object identity; retain
    # all alias relationships inside each actual lifetime, and every raw word.
    scene=None;pointers={};result=[]
    def normalize(value):
        if isinstance(value,dict):return{key:normalize(item)for key,item in value.items()}
        if isinstance(value,list):return[normalize(item)for item in value]
        if isinstance(value,str)and value.startswith('0x'):
            if int(value,16)==0:return {'pointer':0}
            if value not in pointers:pointers[value]=len(pointers)+1
            return {'pointer':pointers[value]}
        return value
    for row in rows:
        if row.get('event')not in EVENTS:continue
        if row['scene']!=scene:
            pointers={};scene=row['scene']
        result.append(normalize(row))
    return result
