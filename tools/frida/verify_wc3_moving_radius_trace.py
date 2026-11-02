#!/usr/bin/env python3
"""Verify public moving Chaos resize, footprint boundaries and deferred commit."""
import argparse
import ctypes
import hashlib
import json
import struct
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_selected_queued_trace import canonical, digest, motion_words, owner_order


def radius_states(rows):
    return [{k:v for k,v in r.items() if k not in ('ms','mover','fine','proximity')}
            for r in rows if r.get('event')=='mover-radius-state']


def primary_events(rows):
    # Secondary presentation time has wall cadence; retain it in the archive,
    # but certify only the deterministic simulation domain here.
    return [{k:v for k,v in r.items() if k not in ('ms','context','secondary')}
            for r in rows if r.get('event')=='chaos-clock' and r['phase'] in
            ('chaos-enabled','chaos-research','chaos-commit','unit-type-rebind')]


def handoff_events(rows):
    return [{k:v for k,v in r.items() if k not in ('ms','context','secondary')}
            for r in rows if r.get('event')=='chaos-clock' and r['phase'] in
            ('move-task-reissue','move-point-task','radius-publication')]


def rearm_events(rows):
    result=[]
    for r in rows:
        if r.get('event')!='public-timer-rearm':continue
        result.append(dict(timerClock=r['timerClock'],primary=r['primary'],counter=r['counter'],
                           request=[r['request'][i] for i in (0,1,2,4,5)],
                           after=[r['after'][i] for i in (0,1,2,4,5)]))
    return result


def verify_producer(spec):
    clocks=spec['clocks'];stages=spec['stages']
    if len(clocks)!=3*len(stages):raise ValueError('Chaos timer extent differs')
    for i,stage in enumerate(stages):
        a,b,c=clocks[3*i:3*i+3]
        if [r['phase'] for r in (a,b,c)]!=[stage,'chaos-commit','unit-type-rebind']:
            raise ValueError('Chaos timer ordering differs')
        unpack=lambda w:struct.unpack('<f',struct.pack('<I',w))[0]
        # Exact primary words are frozen below. General timer deadline arithmetic
        # is NUM-02.9; a single scalar add differs by one ulp in this capture.
        delay=unpack(b['primary'][0])-unpack(a['primary'][0])
        if (abs(delay-.01)>0.00002 or b['primary']!=c['primary'] or
            b['counter']!=c['counter'] or a['primary'][1:]!=b['primary'][1:]):
            raise ValueError('Chaos independent ten-ms primary deadline differs')
    if spec['name']=='research':return
    counts=spec['case_commits'];states=spec['states'];offset=0
    if len(states)!=sum(counts):raise ValueError('moving radius owner extent differs')
    for n,(count,radius,width) in enumerate(zip(counts,spec['radii'],spec['widths'],strict=True)):
        part=states[offset:offset+count];offset+=count
        if ([r['radius'] for r in part]!=[1064828928]*34+[radius]*(count-34) or
            any(r['identity']!=part[0]['identity'] for r in part) or
            part[0]['group']==part[34]['group']):
            raise ValueError('resize must retain mover identity and replace physical owner')
        for j,r in enumerate(part):
            y0,x0,y1,x1=r['fineRect'];w=2 if j<34 else width
            if (y1-y0,x1-x0)!=(w,w):raise ValueError('runtime footprint boundary class differs')
        before=[r for r in spec['morph_markers'] if ('case='+str(n)+' ') in r or spec['name']=='grow']
        immediate=[r for r in before if 'label=after ' in r]
        if len(immediate)!=1 or 'type=1751543663 ' not in immediate[0]:
            raise ValueError('public type replacement must be deferred')
        samples=[r for r in before if 'label=' not in r]
        target=spec['types'][n]
        if not samples or not samples[-1].endswith('chaos=0') or 'type='+str(target)+' ' not in samples[-1]:
            raise ValueError('public replacement type/ability consumption differs')
    if not spec['markers'][-1].endswith('order=0') or 'label=complete ' not in spec['markers'][-1]:
        raise ValueError('moving resize journey must naturally complete')
    if 'rearms' in spec:
        from sys import path
        path.insert(0,str(Path(__file__).resolve().parents[1]/'ghidra'))
        from verify_wc3_pathing_numeric import add
        last={};counts={}
        for r in spec['rearms']:
            before,after=r['request'],r['after'];period=before[2]
            if (before[1]!=r['timerClock'][0] or after[1]!=add(before[1],period) or
                period not in (0x3cf5c290,0x3dcccccd) or before[1]!=last.get(period,period)):
                raise ValueError('periodic owner/public timer must rearm from retained scalar deadline')
            last[period]=after[1];counts[period]=counts.get(period,0)+1
        if counts!={0x3cf5c290:3000,0x3dcccccd:899}:raise ValueError('periodic timer witness extent differs')


def render_header(fixture):
    out='/* Public point Move through original Chaos collision resize. */\n'
    for name in ('grow','matrix'):
        if name=='matrix':out+='\n/* Nine independent public movers; the sole actor in each case is role zero. */\n'
        out+='static uint32_t const moving_radius_'+name+'_motion[][7]={\n'
        out+=''.join('    {'+','.join(str(v)+'u' for v in row)+'},\n' for row in fixture['motions'][name])+'};\n'
    return out


def verify_lifecycle(rows,fixture,case):
    spec=fixture['journeys'][case['journey']];verify_producer(spec)
    meta=[r for r in rows if r.get('event')=='metadata'];end=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('moving radius capture provenance differs')
    if len(end)!=1 or not end[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('moving radius observer incomplete/failed')
    for event,count in spec['event_counts'].items():
        if sum(r.get('event')==event for r in rows)!=count:raise ValueError('moving radius observer extent differs: '+event)
        if event in end[0]['counts'] and end[0]['counts'][event]!=count:raise ValueError('moving radius closing count differs')
    if (digest(canonical(rows))!=spec['phases_sha256'] or owner_order(rows)!=spec['owner_order'] or
        primary_events(rows)!=spec['clocks'] or radius_states(rows)!=spec['states'] or
        [r['value'] for r in rows if r.get('event')=='marker']!=spec['markers'] or
        [r['value'] for r in rows if r.get('event')=='morph-marker']!=spec['morph_markers']):
        raise ValueError('moving radius absolute words/public lifecycle differ')
    if 'rearms' in spec and (rearm_events(rows)!=spec['rearms'] or handoff_events(rows)!=spec['handoffs']):
        raise ValueError('moving radius scalar timer/type-task handoff differs')
    motion=motion_words(rows)
    if spec['name']!='research':
        if any(len(r['members'])!=1 for r in rows if r.get('event')=='pair-group-phase-begin'):
            raise ValueError('independent radius case must contain exactly one actor')
        if [[0,*r[1:]] for r in motion]!=fixture['motions'][spec['name']]:raise ValueError('moving radius engine words differ')
    return dict(passed=True,journey=spec['name'],group_owner_passes=len(motion),public_resizes=len(spec['stages']))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',type=Path,nargs='+')
    p.add_argument('--fixture',type=Path,required=True);p.add_argument('--engine-library',type=Path,required=True)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine);results=[]
    for path,case in zip(a.traces,fixture['cases'],strict=True):
        rows=[json.loads(l) for l in path.read_text().splitlines()];r=verify_lifecycle(rows,fixture,case)
        r.update(verify_motion(rows,engine,None));r['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(r)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(fixture):raise ValueError('moving radius engine header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=fixture['scope'])
    for field in ('exact_decisions','exact_velocity_commits','group_owner_passes','public_resizes'):
        report[field]=sum(r[field] for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
