#!/usr/bin/env python3
"""Certify local group radius sampling and retained footprints through public mutation."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_selected_queued_trace import canonical,digest,motion_words,owner_order
from verify_wc3_moving_radius_trace import primary_events,handoff_events,radius_states


def radius_records(rows,event):
    result=[]
    for r in rows:
        if r.get('event')!=event:continue
        clean={k:v for k,v in r.items() if k not in ('ms','group','shared','path','members')}
        clean['members']=[{k:v for k,v in m.items() if k!='resolved'} for m in r['members']]
        result.append(clean)
    return result


def footprint_words(states):
    # Original16c940's unbound branch scans these resolved live member radii.
    # This derived maximum is separate from the directly observed path+b4 word.
    roles=[]
    for r in states:
        for m in r['members']:
            if m['identity'] not in roles:roles.append(m['identity'])
    return [[r['counter'],sum(1<<roles.index(m['identity']) for m in r['members']),
             max(m['radius'] for m in r['members']),r['footprint']] for r in states]


def verify_producer(spec):
    routing=spec['routing'];scene=spec['name'];initial=1064828928 if scene=='grow' else 1073479680
    final=1073479680 if scene=='grow' else 1046478848
    if len(routing)!=(1 if scene=='remove' else 2):raise ValueError('routing admission extent differs')
    first=routing[0]
    if (len(first['members'])!=2 or first['sharedIdentity'] is not None or
        first['result']!=initial or max(m['radius'] for m in first['members'])!=initial):
        raise ValueError('initial local group maximum differs')
    if scene!='remove':
        last=routing[1]
        if (len(last['members'])!=1 or last['sharedIdentity'] is not None or last['result']!=final or
            last['members'][0]['radius']!=final or last['members'][0]['identity']!=first['members'][1]['identity'] or
            last['identity']==first['identity']):raise ValueError('rebound member must retain identity and admit a new radius owner')
    markers=spec['markers'];public=spec['public']
    if (sum('label=group_radius_order_accepted ' in m for m in markers)!=1 or
        any('label=move_accepted ' in m or 'rejected' in m for m in markers) or
        not markers[-1].endswith('order=0') or 'label=complete ' not in markers[-1] or len(public)!=300):
        raise ValueError('group producer must issue only its common Move and naturally complete')
    if any('peerHandle=1048700 ' not in m for m in public):raise ValueError('public peer handle changed')
    if 'peerOrder=0 ' not in public[-1] or not public[-1].endswith('grow=0 shrink=0'):
        raise ValueError('peer did not finish or Chaos was not consumed')
    expected_type={'grow':1749240903,'shrink':1749240915,'remove':0}[scene]
    if 'peerType='+str(expected_type)+' ' not in public[-1]:raise ValueError('replacement/removal public type differs')
    if 'footprints' in spec:
        states=spec['footprints']
        if len(states)!=spec['owner_passes']:raise ValueError('footprint owner extent differs')
        old=[r for r in states if r['identity']==first['identity']]
        if (not old or any(r['footprint']!=initial or r['sharedIdentity'] is not None for r in old) or
            not any(len(r['members'])==1 for r in old)):
            raise ValueError('survivor must retain the old route footprint after peer leaves')
        if scene!='remove':
            new=[r for r in states if r['identity']==routing[1]['identity']]
            if not new or any(r['footprint']!=final for r in new):raise ValueError('new owner footprint differs')
        for r in states:
            if not r['members'] or any(m['owner']!=r['identity'] for m in r['members']):
                raise ValueError('footprint observer includes stale membership')


def render_header(fixture):
    out='/* Literal public group Move through member growth, shrink and removal. */\n'
    for scene in ('grow','shrink','remove'):
        spec=fixture['journeys'][scene]
        out+='\nstatic uint32_t const group_radius_'+scene+'_motion[][7]={\n'
        out+=''.join('    {'+','.join(str(v)+'u' for v in r)+'},\n' for r in spec['motion'])+'};\n'
        if 'footprints' in spec:
            out+='\nstatic uint32_t const group_radius_'+scene+'_footprints[][4]={\n'
            out+=''.join('    {'+','.join(str(v)+'u' for v in r)+'},\n' for r in footprint_words(spec['footprints']))+'};\n'
    return out


def verify_lifecycle(rows,fixture,case):
    spec=fixture['journeys'][case['journey']];verify_producer(spec)
    meta=[r for r in rows if r.get('event')=='metadata'];end=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('group radius provenance differs')
    if len(end)!=1 or not end[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('group radius capture incomplete/failed')
    for event,n in spec['event_counts'].items():
        if sum(r.get('event')==event for r in rows)!=n:raise ValueError('group radius extent differs: '+event)
        if event in end[0]['counts'] and end[0]['counts'][event]!=n:raise ValueError('group radius closing count differs')
    if (digest(canonical(rows))!=spec['phases_sha256'] or owner_order(rows)!=spec['owner_order'] or
        motion_words(rows)!=spec['motion'] or radius_records(rows,'group-routing-radius')!=spec['routing'] or
        radius_states(rows)!=spec['states'] or primary_events(rows)!=spec['clocks'] or handoff_events(rows)!=spec['handoffs'] or
        [r['value'] for r in rows if r.get('event')=='marker']!=spec['markers'] or
        [r['value'] for r in rows if r.get('event')=='group-radius-marker']!=spec['public']):
        raise ValueError('group radius phase/motion/producer words differ')
    observed=radius_records(rows,'group-footprint-state')
    if case['witness'] and observed!=spec['footprints']:raise ValueError('retained footprint witness differs')
    if not case['witness'] and observed:raise ValueError('base observer unexpectedly contains a footprint witness')
    return dict(passed=True,journey=spec['name'],group_owner_passes=spec['owner_passes'],
                routing_radius_queries=len(spec['routing']),footprint_samples=len(observed))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine);results=[]
    for path,case in zip(a.traces,fixture['cases'],strict=True):
        rows=[json.loads(l) for l in path.read_text().splitlines()];r=verify_lifecycle(rows,fixture,case)
        r.update(verify_motion(rows,engine,None));r['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(r)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(fixture):raise ValueError('group radius C header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=fixture['scope'])
    for k in ('group_owner_passes','routing_radius_queries','footprint_samples','exact_velocity_commits','exact_decisions'):
        report[k]=sum(r[k] for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
