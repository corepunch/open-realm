#!/usr/bin/env python3
"""Verify native ground/flight/ground travel and teleport cancellation."""
import argparse,collections,ctypes,hashlib,json,re
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_selected_queued_trace import canonical,digest,motion_words,owner_order
from verify_wc3_target_overlap_trace import overlap_lifecycle
from verify_wc3_movement_bypasses_trace import producers,routing


def spatial_record(r):
    out={k:v for k,v in r.items()if k not in('event','ms','live','references')}
    out['chain']=[{k:v for k,v in o.items()if k not in('live','references')}for o in out['chain']]
    return out


def spatial(rows):return [spatial_record(r)for r in rows if r.get('event')=='mode-spatial-state']


def phases(rows):
    return [{k:v for k,v in r.items()if k not in('ms','context','secondary')}for r in rows if r.get('event')=='chaos-clock']


def boundary_states(rows):
    state=world=own=None;primary=[0,0,0x43960000];mask=0x02000002;rawcode=category=None;result=[]
    for r in rows:
        if r.get('event')=='position-query':
            state=[r['after'][i]for i in(0,2,3,4,5,7)];world=r['output'];primary=r['clock']
        if r.get('event')=='movement-mask-publication':mask=r['pathMask'];rawcode=r['rawcode'];category=r['objectCategory']
        if r.get('event')=='mode-spatial-state':own=spatial_record(r)
        if r.get('event')=='marker' and 'label=sample 'not in r['value']:
            result.append(dict(marker=r['value'],state=state,world=world,primary=primary,mask=mask,rawcode=rawcode,category=category,spatial=own))
    return result


def chains(rows):
    out=[]
    for r in rows:
        if r.get('event')=='cell-links':
            if r['truncated'] or (r['x'],r['y'])!=(24,26) or r['records']:raise ValueError('complete empty wall watch chain required')
            out.append({k:r[k]for k in('marker','x','y','records','truncated')})
    return out


def verify_contract(s):
    marks=[re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)',x)for x in s['markers']]
    if len(marks)!=309 or any(m is None for m in marks) or [int(m[1])for m in marks if m[2]=='sample']!=list(range(1,301)):raise ValueError('complete mode timer lifetime required')
    expected=[(0,'start_movement_modes'),(10,'point_move'),(40,'fly_requested'),(85,'fly_teleport'),(90,'fly_move'),(120,'ground_requested'),(150,'ground_teleport'),(160,'ground_move'),(300,'complete')]
    if [(int(m[1]),m[2])for m in marks if m[2]!='sample']!=expected:raise ValueError('native movement mode order differs')
    for key,n in [('motion',737),('states',9),('chains',8),('destinations',5),('routes',30),('spatial_queries',643),('mode_phases',20)]:
        if len(s[key])!=n:raise ValueError('incomplete mode lifetime: '+key)
    for i,r in enumerate(s['states']):
        air=i in(3,4,5)
        if (r['rawcode'],r['mask'],r['category'])!=((0x68414952,0x04000004,0x01000000)if air else(0x68563830,0x02000002,0x010000ca)):raise ValueError('authored movement mode publication differs')
        if r['world'][2]:raise ValueError('flat physical support differs')
        own=r['spatial'];box=own['box']
        if own['pathFlags']>>30!=(3 if air else 0) or own['category']!=r['category'] or own['pathMask']!=r['mask']:raise ValueError('path class/query/occupancy publication differs')
        if box[2]-box[0]!=3 or box[3]-box[1]!=3 or not any(o['kind']==1 and o['rectangle']==box and o['category']==r['category']for o in own['chain']):raise ValueError('active class-sized mover links required')
        if i in(3,6) and (r['state'][3:5]!=[0,0] or not r['marker'].endswith('order=0')):raise ValueError('teleport must cancel movement')
    if digest(s['motion'])!=s['motion_sha256']:raise ValueError('committed mode movement words changed')


def render_header(s):
    u=lambda row:','.join(str(v)+'u'for v in row)
    out='/* Literal original ground/flight/ground and public teleport journey. */\nstatic uint32_t const mode87_motion[][7]={\n'+''.join('    {'+u(r)+'},\n'for r in s['motion'])+'};\n'
    out+='static struct { cstring_t marker; uint32_t state[6],world[3],primary[3],mask,rawcode,category; int box[4]; } const mode87_states[]={\n'
    for r in s['states']:out+='    {'+json.dumps(r['marker'])+',{'+u(r['state'])+'},{'+u(r['world'])+'},{'+u(r['primary'])+'},'+u([r['mask'],r['rawcode'],r['category']])+',{'+','.join(map(str,r['spatial']['box']))+'}},\n'
    return out+'};\nstatic cstring_t const mode87_markers[]={\n'+''.join('    '+json.dumps(m)+',\n'for m in s['markers'])+'};\n'


def verify(rows,s,case,engine):
    verify_contract(s)
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items()if k not in('event','pid')}!=case['metadata']:raise ValueError('mode capture provenance differs')
    if len(ends)!=1 or not ends[0].get('installed') or any(r.get('event')=='trace-failed' or r.get('type')=='error'for r in rows):raise ValueError('mode observer failed/incomplete')
    if dict(collections.Counter(r.get('event')for r in rows))!=case['event_counts'] or ends[0]['counts']!=case['observer_counts']:raise ValueError('mode observer extents differ')
    pairs=[(motion_words(rows),'motion'),(producers(rows),'producers'),(boundary_states(rows),'states'),(chains(rows),'chains'),(routing(rows),'routes'),(overlap_lifecycle(rows),'lifecycle'),(owner_order(rows),'owner_order'),(spatial(rows),'spatial_queries'),(phases(rows),'mode_phases')]
    for actual,key in pairs:
        if actual!=s[key]:raise ValueError('mode native contract differs: '+key)
    if digest(canonical(rows))!=s['phases_sha256'] or [r['value']for r in rows if r.get('event')=='marker']!=s['markers'] or [r['destination']for r in rows if r.get('event')=='point-task']!=s['destinations']:raise ValueError('mode owner/public order differs')
    loaded=[r for r in rows if r.get('event')=='map-load-complete']
    if len(loaded)!=1 or (loaded[0]['width'],loaded[0]['height'])!=(64,64) or loaded[0]['cells']!=s['terrain'] or digest(loaded[0]['hierarchy'])!=s['hierarchy_sha256']:raise ValueError('mode authored terrain differs')
    result=verify_motion(rows,engine,None);result.update(verify_primary(rows,engine,s))
    result.update(passed=True,boundary_states=9,cell_chains=8,point_orders=5,route_searches=15,spatial_queries=643,motion_sha256=s['motion_sha256']);return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path);p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    s=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    engine.pathing_heading_error.argtypes=[ctypes.c_uint32]*3;engine.pathing_heading_error.restype=ctypes.c_uint32
    results=[]
    for path,case in zip(a.traces,s['cases'],strict=True):
        r=verify([json.loads(l)for l in path.read_text().splitlines()],s,case,engine);r['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(r)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(s):raise ValueError('mode literal engine header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=s['scope'])
    for key in('exact_velocity_commits','exact_decisions','owner_callbacks','boundary_states','cell_chains','point_orders','route_searches','spatial_queries'):report[key]=sum(r[key]for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
