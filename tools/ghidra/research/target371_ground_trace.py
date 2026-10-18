#!/usr/bin/env python3
"""Retain complete public Ground range producers, including rejected boundaries."""
import json
from pathlib import Path
from target371_trace import SHA,EVENTS as POINT_EVENTS,arithmetic

EVENTS=POINT_EVENTS|{'ground-command','ground-validation','ground-failure-enter','ground-failure-leave','ground-recover-enter','ground-recover-leave','ground-transport-enter','ground-transport-leave','ground-ai-immediate','ground-ai-append','ground-guard-return'}
VARIANTS={
    'ground':dict(markers=378,points=[0x44c00000,0x44080000,0x44e80000,0x44080000],
        ranges=[0x42b40000,0x7f7fffff,0x448fc000,None]),
    'ground-boundary':dict(markers=566,points=[0x441e4000,0x441e4045,0x441e4046,0x44468000,0x4446801d,0x4446801e],
        ranges=[0x7f7fffff,0x7f7fffff,0x42b40000,None,None,0x7f7fffff]),
    'ground-rooted':dict(markers=190,points=[0x44080000,0x44c00000],ranges=[0x7f7fffff,0x43000000],physical=False),
    'ground-ai':dict(markers=370),
    'ground-ai-queue':dict(markers=370),
    'ground-ai-birth':dict(markers=102)}


QUEUE_EVENTS={"queue-"+name+"-"+phase for name in (
    "internal-dispatch","internal-pop","user-advance","user-pop","order-admit",
    "user-append","user-dispatch","ai-scope","depth-acquire","depth-release")
    for phase in ("enter","leave")}
ALL_EVENTS=EVENTS|QUEUE_EVENTS
POINTER_PATHS={('unit',),('unit','pointer'),('order','pointer'),('ability',),
    ('bridge',),('receiver',),('association',),('before','mover'),('after','mover')}


def queue_project(rows,variant):
    """The fixtures contain one live observed source at a time. Queue snapshots
    publish its canonical identity before any dependent physical observation.
    Native allocation reuse may rebind a pointer to a later canonical source;
    aliases are scoped to that explicit identity, never to public scene labels.
    No caller, raw identity, float word or arbitrary hexadecimal string changes.
    """
    if variant=='ground-ai-queue':validate_ai(rows)
    pointers={};unit_pointers={};owner=None;result=[]
    def normalize(value,path=()):
        if isinstance(value,dict):return{k:normalize(v,path+(k,))for k,v in value.items()}
        if isinstance(value,list):return[normalize(v,path+(index,))for index,v in enumerate(value)]
        if path in POINTER_PATHS and isinstance(value,str)and value.startswith('0x'):
            if int(value,16)==0:return dict(pointer=0)
            assert owner is not None,'pointer has no canonical source'
            aliases=pointers.setdefault(owner,{})
            if value not in aliases:aliases[value]=len(aliases)+1
            return dict(unitIdentity=list(owner),pointer=aliases[value])
        return value
    for row in rows:
        if row.get('event')not in ALL_EVENTS:continue
        if row['event']=='marker':result.append(row);continue
        unit=row.get('unit')
        if isinstance(unit,dict):
            identity=unit.get('identity')
            assert isinstance(identity,list)and len(identity)==2 and all(isinstance(v,int)for v in identity),'missing canonical source identity'
            owner=tuple(identity)
            if 'pointer'in unit:unit_pointers[unit['pointer']]=owner
        elif isinstance(unit,str):
            assert unit in unit_pointers,'untracked/cross-source unit pointer'
            owner=unit_pointers[unit]
        assert owner is not None,'missing observed source lifetime'
        result.append(normalize(row))
    assert len(result)==(1815 if variant=='ground-ai-queue'else 639),'incomplete queue composition'
    return result


def load(path,variant):
    rows=[json.loads(line)for line in Path(path).read_text().splitlines()]
    return validate(rows,variant)


def validate(rows,variant):
    count=VARIANTS[variant]['markers']
    assert rows and rows[0].get('event')=='metadata','missing provenance'
    meta=rows[0]
    assert meta.get('task')=='TARGET-01.2-payoff371'and meta.get('owned')and meta.get('sha256')==SHA,'wrong owned image/task'
    assert not any(r.get('type')=='error'or r.get('event')in('trace-failed','script-error')for r in rows),'failed capture'
    assert rows[-1].get('event')=='preload-file'and rows[-1].get('complete')and rows[-1].get('markers')==count,'incomplete public capture'
    if meta['mode']=='observe':
        endings=[r for r in rows if r.get('event')=='trace-end']
        assert len(endings)==1 and endings[0].get('installed'),'missing observer completion'
        assert set(endings[0]['counts'])<=ALL_EVENTS|{'module'},'unknown native observer event'
        for kind in ALL_EVENTS|{'module'}:
            assert endings[0]['counts'].get(kind,0)==sum(r.get('event')==kind for r in rows),'truncated '+kind
        markers=[r['value']for r in rows if r.get('event')=='marker']
        assert len(markers)==count and'label=complete'in markers[-1],'incomplete producer'
    else:
        assert meta['mode']=='control'and not any(r.get('event')in ALL_EVENTS|{'module','trace-end'}for r in rows),'control contains hooks'
    return rows


def project(rows,variant):
    validate(rows,variant);assert rows[0]['mode']=='observe','observed state required'
    spec=VARIANTS[variant]
    if variant in ('ground-ai-queue','ground-ai-birth'):return queue_project(rows,variant)
    if variant=='ground-ai':validate_ai(rows)
    for scene,(point,authored)in enumerate(zip(spec.get('points',[]),spec.get('ranges',[]))):
        own=[r for r in rows if r.get('scene')==scene]
        tasks=[r for r in own if r.get('event')=='point-task']
        requests=[r for r in own if r.get('event')=='point-request']
        ranges=[r for r in own if r.get('event')=='range-publication']
        after=next(r for r in own if r.get('event')=='marker'and'label=issue-after'in r['value'])
        if authored is None:
            assert not tasks and not requests and not ranges,'rejected point produced physical work'
            assert'accepted=0'in after['value']and'order=0'in after['value'],'minimum range not rejected'
        else:
            assert len(tasks)==2 and[t['code']for t in tasks]==[0xd0169,0xd016b],'point task sequence'
            assert tasks[0]['range']==0 and tasks[1]['range']==authored,'selected/in-range range source'
            assert all(t['point']==[point,0x44800000]for t in tasks),'raw click altered'
            if not spec.get('physical',True):
                assert not requests and not ranges,'rooted source acquired a physical Move owner'
                assert 'accepted=1'in after['value'],'rooted public admission changed'
                assert ('order=851984'if scene==0 else'order=0')in after['value'],'rooted near hold/far completion changed'
                validations=[r for r in own if r.get('event')=='ground-validation']
                assert len(validations)==2 and all(r['result']==0 and r['minimum']==0 for r in validations),'rooted point validation changed'
                assert not any(r.get('event')in('arrival','velocity','move-arrival','move-cantpath')for r in own),'rooted source ran a physical owner'
                continue
            assert len(requests)==len(ranges)==1,'missing synchronous physical owner'
            request=requests[0];publication=ranges[0]
            assert request['point']==[point,0x44800000]and request['range']==authored,'actual physical request differs'
            assert(request['flag'],request['arrival'],request['cantpath'])==(1,0xd0196,0xd0198),'completion contract'
            normalized=0x7cffffff if authored==0x7f7fffff else authored-0x2800000
            assert publication['value']==publication['after']['range']==normalized,'canonical range differs'
            before=next(r for r in own if r.get('event')=='marker'and'label=issue-before'in r['value'])
            assert rows.index(before)<rows.index(tasks[0])<rows.index(tasks[1])<rows.index(request)<rows.index(publication)<rows.index(after),'admission delayed'
            assert'accepted=1'in after['value']and'order=851984'in after['value'],'accepted owner missing'
        if variant=='ground-boundary':
            validations=[r for r in own if r.get('event')=='ground-validation']
            assert len(validations)==(1 if authored is None else 2),'public validator count'
            assert all(r['code']==0xd0010 and r['point']==[point,0x44800000]and
                r['minimum']==(0 if scene<3 else 0x437a0000)and
                r['result']==(0x53 if authored is None else 0)for r in validations),'actual minimum boundary differs'
    scene=None;pointers={};result=[]
    def normalize(value):
        if isinstance(value,dict):return{k:normalize(v)for k,v in value.items()}
        if isinstance(value,list):return[normalize(v)for v in value]
        if isinstance(value,str)and value.startswith('0x'):
            if int(value,16)==0:return dict(pointer=0)
            if value not in pointers:pointers[value]=len(pointers)+1
            return dict(pointer=pointers[value])
        return value
    for row in rows:
        if row.get('event')not in EVENTS:continue
        if variant!='ground-ai'and row['scene']!=scene:pointers={};scene=row['scene']
        result.append(normalize(row))
    return result


def validate_ai(rows):
    events=lambda kind:[r for r in rows if r.get('event')==kind]
    requests=events('point-request');publications=events('range-publication')
    assert len(requests)==len(publications)==3,'missing Ground/guard physical request'
    points=[[0x447c0000,0x44820000]]*2+[[0x43880000,0x43980000]]
    for request,publication,point,value in zip(requests,publications,points,[0x42b40000,0x42b40000,0]):
        assert request['point']==point and request['range']==value,'failed destination or guard changed'
        assert(request['flag'],request['arrival'],request['cantpath'])==(1,0xd0196,0xd0198),'completion contract'
        normalized=0x3efae148 if value==0 else 0x40340000
        assert publication['value']==publication['after']['range']==normalized,'canonical range changed'
    tasks=events('point-task')
    assert len(tasks)==5 and[t['code']for t in tasks]==[0xd0169,0xd016b,0xd0169,0xd016b,0xd016b],'Ground/guard task composition'
    assert[t['range']for t in tasks]==[0,0x42b40000,0,0x42b40000,0],'selected Ground ranges changed'
    assert[t['point']for t in tasks]==[points[0]]*4+[points[2]],'raw Ground/guard points changed'
    enters=events('ground-failure-enter');leaves=events('ground-failure-leave')
    assert len(enters)==len(leaves)==2,'missing human/computer failure'
    for scene,(enter,leave)in enumerate(zip(enters,leaves)):
        window=rows[rows.index(enter):rows.index(leave)+1]
        recover=[r for r in window if r.get('event')in('ground-recover-enter','ground-recover-leave')]
        assert len(recover)==2,'missing complete recovery'
        assert[enter['flags'],recover[0]['flags'],recover[1]['flags']]==[20+2*scene,20+2*scene,16+2*scene],'recovery flags changed'
        head=enter['unit']['head']
        assert all(r['unit']['head']==head for r in recover),'recovery consumed public Ground head'
        assert recover[0]['unit']['task']!=recover[1]['unit']['task'],'physical task did not retire'
        assert all(r.get('clock')==enter['clock']for r in window if r.get('event')in EVENTS),'recovery ceased being synchronous'
        kinds=[r.get('event')for r in window]
        if scene==0:
            assert leave['unit']['head']==head and leave['flags']==16,'human Ground head changed'
            assert not any(k in kinds for k in ('ground-transport-enter','ground-ai-immediate','ground-guard-return','point-request')),'human gained AI recovery'
        else:
            ordered=['ground-failure-enter','ground-recover-enter','ground-recover-leave','ground-transport-enter','ground-transport-leave','ground-ai-immediate','ground-guard-return','point-request','ground-failure-leave']
            assert[k for k in kinds if k in ordered]==ordered,'transport/AI/guard recovery order changed'
            transport=next(r for r in window if r.get('event')=='ground-transport-leave')
            immediate=next(r for r in window if r.get('event')=='ground-ai-immediate')
            guard=next(r for r in window if r.get('event')=='ground-guard-return')
            assert not transport['found']and immediate['unit']['head']==head,'transport-null fallback changed'
            assert guard['point']==points[2]and leave['unit']['head']!=head,'computer guard replacement missing'
    assert len([r for r in rows if r.get('event')in EVENTS])==1559,'partial native composition'
