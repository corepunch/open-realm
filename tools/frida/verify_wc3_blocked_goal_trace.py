#!/usr/bin/env python3
"""Certify a public ground Move into blocked terrain through retry and cleanup."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_selected_queued_trace import canonical,digest,motion_words,owner_order

EVENTS={'search','route','route-step','retry-init','retry-result','force-arrival',
        'task-arrival','task-recovery','task-cant-path','arrival-evaluation'}
VOLATILE={'ms','path','mover','group','system','unit','ability'}


def lifecycle(rows):
    def clean(value):
        if isinstance(value,dict):return {k:clean(v) for k,v in value.items() if k not in VOLATILE}
        if isinstance(value,list):return [clean(v) for v in value]
        return value
    return [clean(r) for r in rows if r.get('event') in EVENTS]


def render_header(spec):
    return ('/* Literal original public point Move into a blocked static goal. */\n'
        'static uint32_t const blocked_goal_motion[][7]={\n'+
        ''.join('    {'+','.join(str(v)+'u' for v in r)+'},\n' for r in spec['motion'])+'};\n')


def verify_contract(spec):
    marks=spec['markers'];events=spec['lifecycle']
    take=lambda event:[r for r in events if r['event']==event]
    if (len(marks)!=305 or sum('label=order_accepted ' in m for m in marks)!=1 or
        sum('label=start_blocked_goal ' in m for m in marks)!=1 or
        not marks[-1].endswith('order=0') or 'label=complete ' not in marks[-1] or
        any('rejected' in m for m in marks)):
        raise ValueError('blocked goal public producer/admission/cleanup differs')
    routes=take('route');searches=take('search')
    if (len(routes)!=6 or len(searches)!=6 or [r['kind'] for r in routes]!=['acc','acc','fine','acc','fine','fine'] or
        [r['count'] for r in routes]!=[5,4,25,2,6,1] or
        any(r['truncated'] or len(r['points'])!=r['count'] for r in routes) or
        [r['result'] for r in searches]!=[-1,1,1,-1,-1,-1] or
        [r['budget'] for r in searches]!=[5000,400,700,400,700,700]):
        raise ValueError('blocked goal complete/partial route extent differs')
    init=take('retry-init');retry=take('retry-result');forced=take('force-arrival')
    if (len(init)!=1 or init[0]['count']!=2 or init[0]['members']!=1 or init[0]['nativeGoal']!=[1126629376,1119223808] or
        init[0]['ownerBefore']!=init[0]['ownerAfter'] or len(retry)!=2 or
        [(r['counter'],r['before'],r['after'],r['result']) for r in retry]!=[(1261,0,1,1),(1262,1,1,4)] or
        any(r['ownerBefore']!=r['ownerAfter'] or r['target']!=[0,4294967295] for r in retry) or
        len(forced)!=1 or forced[0]['counter']!=1262 or forced[0]['before']!=0 or forced[0]['after']!=65536):
        raise ValueError('blocked goal retry/forced arrival contract differs')
    steps=take('route-step')
    if len(steps)!=207:raise ValueError('blocked goal route-step extent differs')
    first=next(r for r in steps if r['counter']==1261);last=next(r for r in steps if r['counter']==1262)
    # Fine index/count reset alone on retry1. Terminal4 retains both buffers.
    if (first['after'][0]!=4294967295 or first['after'][9]!=1 or last['after'][0]!=0 or
        last['after'][9]!=1 or last['after'][3]!=first['after'][3] or first['after'][1]!=last['after'][1]):
        raise ValueError('blocked goal retry buffer retention differs')
    arrivals=take('arrival-evaluation')
    if (len(arrivals)!=207 or [(r['flags'],r['inRange'],r['result']) for r in arrivals[-2:]]!=[(65536,1,0),(65536,1,1)]):
        raise ValueError('blocked goal forced range still requires arrival heading')
    for event in ('task-arrival','task-recovery','task-cant-path'):
        rows=take(event)
        if (len(rows)!=1 or rows[0]['before']['orderHead']==[-1,-1] or
            rows[0]['after']['taskHead']!=[-1,-1] or rows[0]['after']['orderHead']!=[-1,-1] or
            rows[0]['after']['abilityFlags']!=16 or rows[0]['after']['unitFlags']!=513):
            raise ValueError('blocked goal task/order cleanup differs')
    if len(spec['motion'])!=207 or spec['motion'][-1][4:6]!=[0,0]:
        raise ValueError('blocked goal motion extent/final velocity differs')


def verify_lifecycle(rows,spec,case):
    verify_contract(spec)
    meta=[r for r in rows if r.get('event')=='metadata'];end=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('blocked goal provenance differs')
    if len(end)!=1 or not end[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('blocked goal capture incomplete/failed')
    for event,count in spec['event_counts'].items():
        if sum(r.get('event')==event for r in rows)!=count:raise ValueError('blocked goal event extent differs: '+event)
        if event in end[0]['counts'] and end[0]['counts'][event]!=count:raise ValueError('blocked goal closing count differs')
    # Host/presentation subdivisions vary; each capture must still close its own observed counts.
    for event in ('clock-source-begin','clock-source-end','clock-advance-begin','clock-advance-end'):
        if sum(r.get('event')==event for r in rows)!=end[0]['counts'][event]:raise ValueError('blocked goal clock observer incomplete')
    if (digest(canonical(rows))!=spec['phases_sha256'] or motion_words(rows)!=spec['motion'] or
        owner_order(rows)!=spec['owner_order'] or lifecycle(rows)!=spec['lifecycle'] or
        [r['value'] for r in rows if r.get('event')=='marker']!=spec['markers']):
        raise ValueError('blocked goal literal phase/motion/lifecycle words differ')
    return dict(passed=True,group_owner_passes=207,searches=6,retries=2,forced_arrivals=1,natural_task_cleanups=1)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    spec=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine);results=[]
    for path,case in zip(a.traces,spec['cases'],strict=True):
        rows=[json.loads(l) for l in path.read_text().splitlines()];result=verify_lifecycle(rows,spec,case)
        result.update(verify_motion(rows,engine,None));result.update(verify_primary(rows,engine,spec))
        result['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(spec):raise ValueError('blocked goal C header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=spec['scope'])
    for k in ('group_owner_passes','searches','retries','forced_arrivals','natural_task_cleanups','exact_velocity_commits','exact_decisions','owner_callbacks'):
        report[k]=sum(r[k] for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
