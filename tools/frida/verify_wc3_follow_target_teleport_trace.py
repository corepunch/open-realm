#!/usr/bin/env python3
"""Verify Follow destination refresh through public target teleports."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_follow_velocity_trace import policy,verify_policy
from verify_wc3_selected_queued_trace import canonical,digest,motion_words,owner_order


def verify_teleport(records,markers,target_markers,tick,mode):
    verify_policy(records,markers,target_markers,marker_count=312,require_same_bucket=False)
    for label in ('before_target_teleport','after_target_teleport'):
        found=[r for r in markers if 'label='+label+' ' in r]
        if len(found)!=1 or not found[0].startswith('PATHTRACE tick='+str(tick)+' ') or not found[0].endswith('order=851971'):
            raise ValueError('public target teleport must preserve the follower head')
    before=[r for r in target_markers if r.startswith('PATHTARGET tick='+str(tick-1)+' ')]
    after=[r for r in target_markers if r.startswith('PATHTARGET tick='+str(tick)+' ')]
    expected=851986 if tick==90 and mode=='xy' else 0
    if (len(before)!=1 or len(after)!=1 or not after[0].endswith('order='+str(expected)) or
            ' x=-1600.000 y=300.000 ' not in after[0] or (tick==90 and not before[0].endswith('order=851986'))):
        raise ValueError('target teleport position/current-order policy differs')
    if not any(r['event']=='replan-check' and r['changed'] and r['destination'][0]>=170 for r in records):
        raise ValueError('teleported target destination never reaches the route')


def render_header(fixture):
    out=''
    for name,rows in fixture['motions'].items():
        out+='/* Original public target teleport: '+name+'. */\n'
        out+='static uint32_t const follow_target_'+name+'_motion[][7]={\n'
        out+=''.join('    {'+','.join(str(v)+'u' for v in row)+'},\n' for row in rows)
        out+='};\n'
    return out


def verify_lifecycle(rows,fixture,case):
    metadata=[r for r in rows if r.get('event')=='metadata'];ending=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or {k:v for k,v in metadata[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('target teleport source provenance differs')
    if len(ending)!=1 or not ending[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('target teleport observer incomplete/failed')
    spec=fixture['journeys'][case['journey']]
    for event,n in spec['event_counts'].items():
        if sum(r.get('event')==event for r in rows)!=n or (not event.startswith('pair-group-phase-') and ending[0]['counts'].get(event)!=n):
            raise ValueError('target teleport observer extent differs: '+event)
    records=policy(rows);markers=[r['value'] for r in rows if r.get('event')=='marker']
    target=[r['value'] for r in rows if r.get('event')=='target-marker']
    verify_teleport(records,markers,target,spec['tick'],spec['mode'])
    if records!=spec['policy'] or markers!=spec['markers'] or target!=spec['target_markers']:
        raise ValueError('target teleport range/refresh/public producer differs')
    motion=fixture['motions'][spec['motion']]
    if digest(canonical(rows))!=spec['phases_sha256'] or motion_words(rows)!=motion or owner_order(rows)!=spec['owner_order']:
        raise ValueError('target teleport absolute motion/owner phases differ')
    return dict(passed=True,journey=case['journey'],motion_sha256=digest(motion),target_reloads=58,group_owner_passes=len(motion),public_teleports=1)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',type=Path,nargs='+')
    p.add_argument('--fixture',type=Path,required=True);p.add_argument('--engine-library',type=Path,required=True)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine);results=[]
    for path,case in zip(a.traces,fixture['cases'],strict=True):
        rows=[json.loads(l) for l in path.read_text().splitlines()];r=verify_lifecycle(rows,fixture,case)
        r.update(verify_motion(rows,engine,None));r['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(r)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(fixture):raise ValueError('target teleport engine header differs')
    report=dict(passed=True,cases=len(results),exact_decisions=sum(r['exact_decisions'] for r in results),
        exact_velocity_commits=sum(r['exact_velocity_commits'] for r in results),target_reloads=58*len(results),
        group_owner_passes=sum(r['group_owner_passes'] for r in results),results=results,scope=fixture['scope'])
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
