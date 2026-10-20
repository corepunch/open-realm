#!/usr/bin/env python3
"""Verify Smart approach/persistent Follow through moving-target speed change and Stop."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_selected_queued_trace import canonical,digest,motion_words,owner_order

POLICY = ('arrival-range','target-refresh','replan-check','group-completion')


def policy(rows):
    return [{k:v for k,v in r.items() if k not in ('ms','group','path','mover','members')}
            for r in rows if r.get('event') in POLICY]


def verify_policy(records,markers,target_markers,marker_count=310,require_same_bucket=True):
    ranges=[r for r in records if r['event']=='arrival-range']
    if [r['value'] for r in ranges]!=[1093992448,1093992448,1056629064]:
        raise ValueError('target arrival range must add both collision radii')
    refresh=[r for r in records if r['event']=='target-refresh']
    if len(refresh)!=58 or any(r['reload']!=max(16,min(132,r['unclamped'])) or r['coefficient']!=0.33000001311302185 for r in refresh):
        raise ValueError('target refresh countdown/reload differs')
    replans=[r for r in records if r['event']=='replan-check']
    if require_same_bucket and not any(r['oldDestination']!=r['destination'] and not r['changed'] for r in replans):
        raise ValueError('same-bucket target motion must retain the destination')
    for r in replans:
        import math
        changed=any(math.floor(a)>>r['shift'] != math.floor(b)>>r['shift'] for a,b in zip(r['oldDestination'],r['destination']))
        ready=not changed or all((r['counter']-t)&0xffffffff>=10 for t in r['timestamps'])
        if (r['changed'],r['ready'])!=(int(changed),int(ready)):
            raise ValueError('destination bucket/readiness differs')
    completion=[r for r in records if r['event']=='group-completion' and r['flags']&1]
    if not completion or any(r['gateOpen'] for r in completion):
        raise ValueError('persistent Follow must retain its user head at range arrival')
    if (len(markers)!=marker_count or len(target_markers)!=300 or
            not any('tick=299 ' in r and r.endswith('order=851971') for r in markers) or
            not any('tick=300 label=complete ' in r and r.endswith('order=0') for r in markers)):
        raise ValueError('bounded Follow must end by explicit authored Stop')


def render_header(fixture):
    out='/* Original scene53: Smart approach, persistent Follow, target travel/speed and explicit Stop. */\n'
    out+='static uint32_t const follow_velocity_motion[][7]={\n'
    for r in fixture['engine_motion']:out+='    {'+','.join(str(v)+'u' for v in r)+'},\n'
    return out+'};\n'


def verify_lifecycle(rows,fixture,case):
    metadata=[r for r in rows if r.get('event')=='metadata']
    ending=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or {k:v for k,v in metadata[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('follow source provenance differs')
    if len(ending)!=1 or not ending[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('follow observer incomplete/failed')
    for ev,n in fixture['event_counts'].items():
        if sum(r.get('event')==ev for r in rows)!=n or (not ev.startswith('pair-group-phase-') and ending[0]['counts'].get(ev)!=n):
            raise ValueError('follow observer extent differs: '+ev)
    actual=policy(rows)
    marks=[r['value'] for r in rows if r.get('event')=='marker']
    target=[r['value'] for r in rows if r.get('event')=='target-marker']
    verify_policy(actual,marks,target)
    if actual!=fixture['policy'] or marks!=fixture['markers'] or target!=fixture['target_markers']:
        raise ValueError('follow cached destination/range/completion producer differs')
    if digest(canonical(rows))!=fixture['phases_sha256'] or motion_words(rows)!=fixture['engine_motion'] or owner_order(rows)!=fixture['owner_order']:
        raise ValueError('follow absolute motion/phase/owner sequence differs')
    return dict(passed=True,motion_sha256=digest(fixture['engine_motion']),target_reloads=58,group_owner_passes=1015,explicit_stops=1)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces',type=Path,nargs='+');p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--engine-library',type=Path,required=True);p.add_argument('--check-engine-header',type=Path)
    p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    results=[]
    for path,case in zip(a.traces,fixture['cases'],strict=True):
        rows=[json.loads(l) for l in path.read_text().splitlines()]
        r=verify_lifecycle(rows,fixture,case);r.update(verify_motion(rows,engine,None))
        r['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(r)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(fixture):raise ValueError('follow engine header differs')
    report=dict(passed=True,cases=len(results),exact_decisions=sum(r['exact_decisions'] for r in results),
        exact_velocity_commits=sum(r['exact_velocity_commits'] for r in results),target_reloads=58*len(results),
        group_owner_passes=1015*len(results),results=results,scope=fixture['scope'])
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
