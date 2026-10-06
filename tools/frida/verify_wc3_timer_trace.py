#!/usr/bin/env python3
"""Pin public scalar timer history and its exact getter-driven Move lifetime."""
import argparse
import hashlib
import json
from pathlib import Path


def normalize(rows):
    def words(values,count):
        if not isinstance(values,list)or len(values)!=count or any(type(v)is not int or not 0<=v<=0xffffffff for v in values):
            raise ValueError('timer scalar words require exact uint32 arrays')
        return values
    def word(value):return words([value],1)[0]
    timer_ids={};handles={};out=[]
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
        elif e=='velocity-commit':out.append(dict(event=e,clock=words(r['clock'],3),position=words(r['after'][2:4],2),velocity=words(r['after'][4:6],2),heading=word(r['after'][7])))
    return out


def verify(raw,fixture,cap):
    if hashlib.sha256(raw).hexdigest()!=cap['sha256']or len(raw)!=cap['bytes']:raise ValueError('timer capture hash/length differs')
    rows=[json.loads(s)for s in raw.splitlines()if s.strip()]
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or len(ends)!=1 or not ends[0].get('installed')or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows):raise ValueError('incomplete/error timer capture')
    if {k:v for k,v in meta[0].items()if k not in ('event','pid')}!=cap['metadata']:raise ValueError('timer producer provenance differs')
    if [r['value']for r in rows if r.get('event')=='timer-marker'and r['value']=='PATHTIMER complete']!=['PATHTIMER complete']:raise ValueError('timer producer completion missing')
    actual=normalize(rows)
    if actual!=fixture['events']:raise ValueError('public timer words/lifecycle/motion differ')
    gets=[r for r in actual if r['event']=='timer-getter']
    if len(gets)!=1600 or len([r for r in actual if r['event']=='velocity-commit'])!=70:raise ValueError('timer/movement cardinality differs')
    for actor in range(13):
        word=fixture['timeout_words'][actor]
        values=[r['word']for r in gets if r['name']=='timeout'and r['actor']==actor]
        if values!=[word]*41:raise ValueError('public timeout changed after expiry/pause/resume')
    return {'public_getters':len(gets),'scalar_getters':sum(r['event']=='timer-scalar-getter'for r in actual),'motion_commits':70,'timeouts':13*41}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',type=Path,nargs=2,required=True);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    f=json.loads(a.fixture.read_text())
    if f.get('version')!=1 or len(f.get('captures',[]))!=2:p.error('requires version1 two-repeat fixture')
    counts=[verify(p.read_bytes(),f,c)for p,c in zip(a.capture,f['captures'])]
    report=dict(passed=True,captures=2,public_getters=sum(c['public_getters']for c in counts),scalar_getters=sum(c['scalar_getters']for c in counts),motion_commits=sum(c['motion_commits']for c in counts),timeout_words=sum(c['timeouts']for c in counts),scope=f['scope'])
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
