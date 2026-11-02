#!/usr/bin/env python3
"""Verify public collision resize, retained Follow range, and fresh approaches."""
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_follow_velocity_trace import policy
from verify_wc3_selected_queued_trace import canonical,digest,motion_words,owner_order


def resize_states(rows):
    return [{k:v for k,v in r.items() if k not in ('ms','group','target')}
            for r in rows if r.get('event')=='resize-target-state']


def verify_resize(spec):
    size,fresh=spec['size'],spec['fresh']
    research=spec.get('research',False)
    old_count=467 if research else 301
    radius,new_type=(1073479680,1749240903) if size=='grow' else (1046478848,1749240915)
    states=spec['resize_states']
    if len(states)!=967 or [r['radius'] for r in states] != [1064828928]*old_count+[radius]*(967-old_count):
        raise ValueError('public morph must actually resize the retained target mover')
    if any(r['groupHandle']!=r['moverHandle'] or r['moverHandle']!=states[0]['moverHandle'] for r in states):
        raise ValueError('resize must retain the target canonical identity')
    target=spec['target_markers'];markers=spec['markers']
    if len(target)!=302 or len(markers)!=(319 if fresh else 314 if research else 313):
        raise ValueError('resize public producer extent differs')
    before=[r for r in target if 'label=before_resize ' in r]
    after=[r for r in target if 'label=after_resize ' in r]
    if (len(before)!=1 or len(after)!=1 or not before[0].startswith('PATHTARGET tick=100 ') or
        ' type=1751543663 ' not in before[0] or ' type=1751543663 ' not in after[0] or
        'handle=1048700' not in before[0] or 'handle=1048700' not in after[0]):
        raise ValueError('public Chaos type change must be deferred and preserve the handle')
    for tick in range(101,301):
        rows=[r for r in target if r.startswith('PATHTARGET tick='+str(tick)+' ')]
        expected_type=1751543663 if research and tick<=150 else new_type
        if len(rows)!=1 or not rows[0].endswith('type='+str(expected_type)+' handle=1048700'):
            raise ValueError('public replacement unit type/handle differs')
    accepted=[r for r in markers if 'label=target_resize_accepted ' in r]
    if len(accepted)!=1 or not accepted[0].endswith('order=851971'):
        raise ValueError('public resize must preserve the Follow head')
    for label,count in [('before_resized_follow',2 if fresh else 0),('resized_follow_accepted',2 if fresh else 0),('after_resized_follow',2 if fresh else 0)]:
        rows=[r for r in markers if 'label='+label+' ' in r]
        if len(rows)!=count or any(not r.startswith('PATHTRACE tick=150 ') or not r.endswith('order=851971') for r in rows):
            raise ValueError('fresh resized Follow admission differs')
    unlock=[r for r in markers if 'label=target_resize_research ' in r]
    if len(unlock)!=(1 if research else 0) or any(not r.startswith('PATHTRACE tick=150 ') or not r.endswith('order=851971') for r in unlock):
        raise ValueError('public research unlock producer differs')
    records=spec['policy']
    ranges=[r['value'] for r in records if r['event']=='arrival-range']
    expected=[1093992448,1093992448,1056629064]
    if fresh:expected += [1088758002,1088758002,1095041024] if size=='grow' else [1093206016]*3
    if ranges!=expected:raise ValueError('retained/fresh radius-sum and half-edge approach range differs')
    refresh=[r for r in records if r['event']=='target-refresh']
    if len(refresh)!=(59 if fresh else 58) or any(r['reload']!=max(16,min(132,r['unclamped'])) or r['coefficient']!=0.33000001311302185 for r in refresh):
        raise ValueError('resized target countdown/reload differs')
    for r in records:
        if r['event']!='replan-check':continue
        changed=any(math.floor(a)>>r['shift'] != math.floor(b)>>r['shift'] for a,b in zip(r['oldDestination'],r['destination']))
        ready=not changed or all((r['counter']-t)&0xffffffff>=10 for t in r['timestamps'])
        if (r['changed'],r['ready'])!=(int(changed),int(ready)):
            raise ValueError('resized target destination bucket/readiness differs')
    completion=[r for r in records if r['event']=='group-completion' and r['flags']&1]
    if not completion or any(r['gateOpen'] for r in completion):
        raise ValueError('resized persistent Follow must retain its user head')
    if (not any('tick=299 ' in r and r.endswith('order=851971') for r in markers) or
        not any('tick=300 label=complete ' in r and r.endswith('order=0') for r in markers)):
        raise ValueError('resized Follow must terminate by the authored Stop')


def render_header(fixture):
    out='/* Original public Chaos resize: retained and twice-reissued Smart. */\n'
    for name,rows in fixture['motions'].items():
        out+='static uint32_t const follow_target_resize_'+name+'_motion[][7]={\n'
        out+=''.join('    {'+','.join(str(v)+'u' for v in row)+'},\n' for row in rows)
        out+='};\n'
    return out


def verify_lifecycle(rows,fixture,case):
    metadata=[r for r in rows if r.get('event')=='metadata'];ending=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or {k:v for k,v in metadata[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('target resize source provenance differs')
    if len(ending)!=1 or not ending[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('target resize observer incomplete/failed')
    spec=fixture['journeys'][case['journey']];verify_resize(spec)
    for event,n in spec['event_counts'].items():
        if sum(r.get('event')==event for r in rows)!=n or (not event.startswith('pair-group-phase-') and ending[0]['counts'].get(event)!=n):
            raise ValueError('target resize observer extent differs: '+event)
    if (policy(rows)!=spec['policy'] or resize_states(rows)!=spec['resize_states'] or
        [r['value'] for r in rows if r.get('event')=='marker']!=spec['markers'] or
        [r['value'] for r in rows if r.get('event')=='target-marker']!=spec['target_markers']):
        raise ValueError('target resize profile/range/public producer differs')
    motion=fixture['motions'][spec['motion']]
    if digest(canonical(rows))!=spec['phases_sha256'] or motion_words(rows)!=motion or owner_order(rows)!=spec['owner_order']:
        raise ValueError('target resize absolute motion/owner phases differ')
    return dict(passed=True,journey=case['journey'],motion_sha256=digest(motion),target_reloads=spec['event_counts']['target-refresh'],group_owner_passes=len(motion),public_resizes=1)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',type=Path,nargs='+')
    p.add_argument('--fixture',type=Path,required=True);p.add_argument('--engine-library',type=Path,required=True)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine);results=[]
    for path,case in zip(a.traces,fixture['cases'],strict=True):
        rows=[json.loads(l) for l in path.read_text().splitlines()];r=verify_lifecycle(rows,fixture,case)
        r.update(verify_motion(rows,engine,None));r['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(r)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(fixture):raise ValueError('target resize engine header differs')
    report=dict(passed=True,cases=len(results),exact_decisions=sum(r['exact_decisions'] for r in results),exact_velocity_commits=sum(r['exact_velocity_commits'] for r in results),target_reloads=sum(r['target_reloads'] for r in results),group_owner_passes=sum(r['group_owner_passes'] for r in results),results=results,scope=fixture['scope'])
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
