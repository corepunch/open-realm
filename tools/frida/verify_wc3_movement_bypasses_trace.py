#!/usr/bin/env python3
"""Certify original pathing toggles, pause/resume and displacement during Move."""
import argparse
import collections
import ctypes
import hashlib
import json
import re
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_selected_queued_trace import canonical, digest, motion_words, owner_order
from verify_wc3_target_overlap_trace import overlap_lifecycle


def producers(rows):
    events={'speed-native','speed-cap-change','speed-publication','mover-stop',
            'position-query','position-commit','position-native','expression-timer-start',
            'pathing-toggle','pause-native','movement-mask-publication'}
    volatile={'ms','mover','unit','ability','bridge','handle','handler'}
    return [{k:v for k,v in r.items() if k not in volatile} for r in rows if r.get('event') in events]


def boundary_states(rows):
    state=world=None;primary=[0,0,0x43960000];paused=False;mask=0x02000002;result=[]
    for r in rows:
        if r.get('event')=='clock-advance-end' and r['domain']==20:primary=r['after'][:3]
        if r.get('event')=='position-query':
            state=[r['after'][i]for i in(0,2,3,4,5,7)];world=r['output'];primary=r['clock']
        if r.get('event')=='pause-native' and r['name']=='IsUnitPaused':paused=bool(r['output'])
        if r.get('event')=='movement-mask-publication':mask=r['pathMask']
        if r.get('event')=='marker' and 'label=sample 'not in r['value']:
            result.append(dict(marker=r['value'],state=state,world=world,primary=primary,paused=paused,mask=mask))
    return result


def chains(rows):
    result=[]
    for r in rows:
        if r.get('event')!='cell-links':continue
        if r['truncated'] or (r['x'],r['y'])!=(24,26):raise ValueError('complete bypass cell chain required')
        values=[]
        for obj in r['records']:
            if obj['kind']not in(0,1) or obj['category']not in(0x010000ca,202):raise ValueError('unclassified bypass occupant')
            values.append({k:obj[k]for k in('kind','rectangle','category','flags')})
        result.append(dict(marker=r['marker'],records=values))
    return result


def routing(rows):
    return [{k:v for k,v in r.items()if k not in('ms','path','system')}
            for r in rows if r.get('event')in('search','route')]


def verify_contract(s):
    marks=[re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)',x)for x in s['markers']]
    if len(marks)!=310 or any(m is None for m in marks) or [int(m[1])for m in marks if m[2]=='sample']!=list(range(1,301)):
        raise ValueError('complete public bypass timer lifetime required')
    expected=[(0,'start_movement_bypasses'),(10,'point_move'),(40,'pathing_off'),(85,'pathing_on'),
              (110,'paused'),(115,'paused_displacement'),(135,'resumed'),(150,'pathing_off'),(165,'pathing_on'),(300,'complete')]
    if [(int(m[1]),m[2])for m in marks if m[2]!='sample']!=expected:raise ValueError('bypass native event order differs')
    if len(s['motion'])!=773 or len(s['states'])!=10 or len(s['chains'])!=9 or len(s['destinations'])!=2:
        raise ValueError('movement bypass state/owner lifetime incomplete')
    for r in s['states']:
        if r['world'][2]:raise ValueError('flat support height differs')
        if 'label=paused' in r['marker'] or 'label=resumed ' in r['marker']:
            if r['state'][3:5]!=[0,0] or not r['marker'].endswith('order=851973'):raise ValueError('pause must Stop and retain the suspended public order')
        if r['paused']!=('label=paused' in r['marker']):raise ValueError('public pause state differs')
        if r['mask']!=(0 if 'label=pathing_off 'in r['marker']else 0x02000002):raise ValueError('own query policy differs')
    if len(s['routes'])!=26:raise ValueError('complete coarse/fine rebuild lifetime required')
    if any(r['objectCategory']!=0x010000ca or r['pathMask']not in(0,0x02000002) for r in s['producers']if r['event']=='movement-mask-publication'):raise ValueError('pathing toggle must retain mover occupancy category')
    if digest(s['motion'])!=s['motion_sha256']:raise ValueError('movement bypass motion words changed')


def float_word(value):return ctypes.c_uint32.from_buffer_copy(ctypes.c_float(value)).value


def render_header(s):
    out='/* Literal original public pathing/pause/displacement journey. */\n'
    out+='static uint32_t const bypass86_motion[][7]={\n'+''.join('    {'+','.join(str(v)+'u'for v in r)+'},\n'for r in s['motion'])+'};\n'
    out+='static struct { cstring_t marker; uint32_t state[6],world[3],primary[3],mask; bool paused,occupied; } const bypass86_states[]={\n'
    for r in s['states']:
        c=next((c for c in s['chains']if c['marker']==r['marker']),None)
        objects=[] if c is None else [o for o in c['records']if o['category']==0x010000ca and o['kind']==1]
        occupied=any(o['rectangle'][0]<=26<o['rectangle'][2] and o['rectangle'][1]<=24<o['rectangle'][3]for o in objects)
        out+='    {'+json.dumps(r['marker'])+',{'+','.join(str(v)+'u'for v in r['state'])+'},{'+','.join(str(v)+'u'for v in r['world'])+'},{'+','.join(str(v)+'u'for v in r['primary'])+'},'+str(r['mask'])+'u,'+str(r['paused']).lower()+','+str(occupied).lower()+'},\n'
    out+='};\nstatic cstring_t const bypass86_markers[]={\n'+''.join('    '+json.dumps(m)+',\n'for m in s['markers'])+'};\n'
    return out


def verify(rows,s,case,engine):
    verify_contract(s)
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items()if k not in('event','pid')}!=case['metadata']:
        raise ValueError('movement bypass capture provenance differs')
    if len(ends)!=1 or not ends[0].get('installed') or any(r.get('event')=='trace-failed' or r.get('type')=='error'for r in rows):
        raise ValueError('movement bypass observer failed/incomplete')
    if dict(collections.Counter(r.get('event')for r in rows))!=case['event_counts'] or ends[0]['counts']!=case['observer_counts']:
        raise ValueError('movement bypass observer extents differ')
    if (motion_words(rows)!=s['motion'] or producers(rows)!=s['producers'] or boundary_states(rows)!=s['states'] or
        chains(rows)!=s['chains'] or routing(rows)!=s['routes'] or overlap_lifecycle(rows)!=s['lifecycle'] or owner_order(rows)!=s['owner_order'] or
        digest(canonical(rows))!=s['phases_sha256'] or [r['value']for r in rows if r.get('event')=='marker']!=s['markers'] or
        [r['destination']for r in rows if r.get('event')=='point-task']!=s['destinations']):
        raise ValueError('movement bypass producer/owner/occupancy/motion differs')
    loaded=[r for r in rows if r.get('event')=='map-load-complete']
    if len(loaded)!=1 or (loaded[0]['width'],loaded[0]['height'])!=(64,64) or loaded[0]['cells']!=s['terrain'] or digest(loaded[0]['hierarchy'])!=s['hierarchy_sha256']:
        raise ValueError('original wall map initialization differs')
    result=verify_motion(rows,engine,None);result.update(verify_primary(rows,engine,s))
    result.update(passed=True,boundary_states=10,cell_chains=9,point_orders=2,route_searches=13,motion_sha256=s['motion_sha256'])
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    s=json.loads(a.fixture.read_text())
    engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    engine.pathing_heading_error.argtypes=[ctypes.c_uint32]*3;engine.pathing_heading_error.restype=ctypes.c_uint32
    results=[]
    for path,case in zip(a.traces,s['cases'],strict=True):
        r=verify([json.loads(l)for l in path.read_text().splitlines()],s,case,engine)
        r['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(r)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(s):raise ValueError('movement bypass C header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=s['scope'])
    for key in('exact_velocity_commits','exact_decisions','owner_callbacks','boundary_states','cell_chains','point_orders','route_searches'):
        report[key]=sum(r[key]for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
