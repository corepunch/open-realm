#!/usr/bin/env python3
"""Pin public scalar timer history and its exact getter-driven Move lifetime."""
import argparse
import hashlib
import json
from pathlib import Path


def normalize(rows,control=False,mutation=False):
    def words(values,count):
        if not isinstance(values,list)or len(values)!=count or any(type(v)is not int or not 0<=v<=0xffffffff for v in values):
            raise ValueError('timer scalar words require exact uint32 arrays')
        return values
    def word(value):return words([value],1)[0]
    timer_ids={};handles={};out=[]
    controls={r["control"] for r in rows if r.get("event")=="timer-dispatch-begin"}
    def identity(table,key):
        if key not in table:table[key]=len(table)
        return table[key]
    for r in rows:
        e=r.get('event')
        if e=='timer-marker':
            if type(r['value'])is not str:raise ValueError('invalid timer marker')
            out.append(dict(event=e,value=r['value']))
        elif e=='timer-start':out.append(dict(event=e,actor=identity(timer_ids,r['timer']),timeout=word(r['timeout']),periodic=word(r['periodic']),clock=words(r['clock'][:3],3),counter=word(r['counter'])))
        elif e=='timer-getter':out.append(dict(event=e,actor=identity(handles,r['handle']),name=r['name'],word=word(r['word']),clock=words(r['clock'][:3],3),counter=word(r['counter'])))
        elif e=='timer-scalar-getter':out.append(dict(event=e,actor=identity(timer_ids,r['timer']),name=r['name'],word=word(r['word']),stored=words(r['stored'],2),request_present=r['request']!='0x0',methods=words(r['clockMethods'],8),clock=words(r['clock'][:3],3),counter=word(r['counter'])))
        if e=='timer-start' and mutation:out[-1]['rootMethod']=word(r['rootMethod'])
        if e=='timer-scalar-getter' and control:
            x=r['control'];q=x['request'];out[-1]['timerClock']=words(r['timerClock'],4)
            if (q is not None)!=out[-1]['request_present']:raise ValueError('timer request presence differs')
            if q is not None:words(q,6)
            if any(word(x[k])>0xffff for k in ('segments','remainingSegments')):raise ValueError('timer segment count exceeds uint16')
            out[-1]['control']={k:word(x[k])for k in ('flags','segments','remainingSegments','residual')}
            out[-1]['control']['request']=None if q is None else [word(q[i])for i in (0,1,3,4)]
        if mutation and e in ('timer-control-begin','timer-control-end','timer-cancel','timer-dispatch-begin','timer-dispatch-end'):
            timer=r.get('timer') or hex(int(r['control'],16)-0x24)
            if e=='timer-cancel' and r['control']not in controls:continue
            value=dict(event=e,actor=identity(timer_ids,timer),clock=words(r['clock'][:3],3),counter=word(r['counter']))
            if 'name'in r:value['name']=r['name']
            if e=='timer-control-end':value['stored']=[word(r['stored'][i])for i in (0,2)]
            if e=='timer-cancel':value['flags']=word(r['flags'])
            if e.startswith('timer-dispatch-'):
                value['timerClock']=words(r['timerClock'],4)
                words(r['words'],7);value['words']=[word(r['words'][i])for i in (0,1,3,4,6)]
                if e=='timer-dispatch-end':
                    words(r['after'],7);value['after']=[word(r['after'][i])for i in (0,1,3,4,6)]
            out.append(value)
        if e=='velocity-commit':out.append(dict(event=e,clock=words(r['clock'],3),position=words(r['after'][2:4],2),velocity=words(r['after'][4:6],2),heading=word(r['after'][7])))
    return out


def normalize_release(rows):
    timers={};events=[]
    for r in rows:
        if r.get('event')=='timer-start'and r['timer']not in timers:timers[r['timer']]=len(timers)
    for r in rows:
        event=r.get('event')
        if event in ('timer-destroy-request','timer-release-queued','timer-release-commit'):
            value=dict(event=event,actor=timers[r['timer']],clock=r['clock'][:3],counter=r['counter'])
            if event=='timer-release-queued':
                value['timerClock']=r['timerClock'];value['words']=[r['words'][i]for i in (0,1,3,4,6)]
            events.append(value)
        elif event=='public-timer-rearm'and r['request'][2]==0x3cf5c290 and r['counter']<=1030:
            value=dict(event=event,timerClock=r['timerClock'],clock=r['primary'][:3],counter=r['counter'],
                before=[r['request'][i]for i in (1,2,4,5)],after=[r['after'][i]for i in (1,2,4,5)])
            events.append(value)
    for r in events:
        for key in ('clock','timerClock','words','before','after'):
            if key in r and any(type(w)is not int or not 0<=w<=0xffffffff for w in r[key]):raise ValueError('release requires uint32 words')
    return events

def verify(raw,fixture,cap):
    if hashlib.sha256(raw).hexdigest()!=cap['sha256']or len(raw)!=cap['bytes']:raise ValueError('timer capture hash/length differs')
    rows=[json.loads(s)for s in raw.splitlines()if s.strip()]
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or len(ends)!=1 or not ends[0].get('installed')or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows):raise ValueError('incomplete/error timer capture')
    if {k:v for k,v in meta[0].items()if k not in ('event','pid')}!=cap['metadata']:raise ValueError('timer producer provenance differs')
    if fixture['version']==4:
        actual=normalize_release(rows)
        if actual!=fixture['events']:raise ValueError('timer release/order lifecycle differs')
        if sum(r['event']=='timer-release-queued'for r in actual)!=4 or sum(r['event']=='timer-release-commit'for r in actual)!=4:raise ValueError('timer release window incomplete')
        if sum(r['event']=='public-timer-rearm'for r in actual)!=6:raise ValueError('owner registration window incomplete')
        return dict(public_getters=0,scalar_getters=0,motion_commits=0,timeouts=0,retirements=4,owner_rearms=6)
    if [r['value']for r in rows if r.get('event')=='timer-marker'and r['value']=='PATHTIMER complete']!=['PATHTIMER complete']:raise ValueError('timer producer completion missing')
    actual=normalize(rows,fixture['version']>=2,fixture['version']==3)
    if actual!=fixture['events']:raise ValueError('public timer words/lifecycle/motion differ')
    gets=[r for r in actual if r['event']=='timer-getter']
    if len(gets)!=fixture.get('public_getters',1600) or len([r for r in actual if r['event']=='velocity-commit'])!=fixture.get('motion_commits',70):raise ValueError('timer/movement cardinality differs')
    if fixture['version']==3:
        counts={name:sum(r['event']==name for r in actual)for name in ('timer-dispatch-begin','timer-dispatch-end','timer-control-begin','timer-control-end','timer-start','timer-cancel')}
        if counts!=fixture['lifecycle_counts']:raise ValueError('timer lifecycle cardinality differs')
        records=sum(r['event']=='timer-marker'and r['value'].startswith('PATHTIMER record=')for r in actual)
        if records*3!=len(gets):raise ValueError('timer callback getter rows differ')
        return dict(public_getters=len(gets),scalar_getters=sum(r['event']=='timer-scalar-getter'for r in actual),motion_commits=fixture['motion_commits'],records=records,**counts)
    for actor in range(13):
        word=fixture['timeout_words'][actor]
        values=[r['word']for r in gets if r['name']=='timeout'and r['actor']==actor]
        if values!=[word]*fixture.get('read_ticks',41):raise ValueError('public timeout changed after expiry/pause/resume')
    return {'public_getters':len(gets),'scalar_getters':sum(r['event']=='timer-scalar-getter'for r in actual),'motion_commits':fixture.get('motion_commits',70),'timeouts':13*fixture.get('read_ticks',41)}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',type=Path,nargs=2,required=True);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    f=json.loads(a.fixture.read_text())
    if f.get('version')not in (1,2,3,4) or len(f.get('captures',[]))!=2:p.error('requires version1/2/3/4 two-repeat fixture')
    counts=[verify(p.read_bytes(),f,c)for p,c in zip(a.capture,f['captures'])]
    report=dict(passed=True,captures=2,public_getters=sum(c['public_getters']for c in counts),scalar_getters=sum(c['scalar_getters']for c in counts),motion_commits=sum(c['motion_commits']for c in counts),timeout_words=sum(c.get('timeouts',0)for c in counts),scope=f['scope'])
    if f['version']==4:report.update(retirements=8,owner_rearms=12)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
