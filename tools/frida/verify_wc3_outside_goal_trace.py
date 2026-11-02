#!/usr/bin/env python3
"""Certify public point routing clipping separately from retained task coordinates."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_selected_queued_trace import canonical,digest,motion_words,owner_order
from verify_wc3_blocked_goal_trace import lifecycle


def producer(rows):
    return dict(bounds=[{k:v for k,v in r.items() if k not in ('ms','event')} for r in rows if r.get('event')=='point-bound-admission'],
        tasks=[dict(destination=r['destination'],range=r['range'],eventCode=r['eventCode']) for r in rows if r.get('event')=='point-task'],
        points=[r['original'] for r in rows if r.get('event')=='path-destination' and r['replaceOriginal']==1 and r['caller']=='0x16dee5'],
        markers=[r['value'] for r in rows if r.get('event')=='marker'],
        public=[r['value'] for r in rows if r.get('event')=='bound-marker'])


def bits(value):return ctypes.c_uint32.from_buffer_copy(ctypes.c_float(value)).value


def verify_producer(spec,engine=None):
    p=spec['producer'];count=1 if spec['name']=='west' else 12
    if len(p['bounds'])!=count or len(p['tasks'])!=count or len(p['points'])!=count:
        raise ValueError('point clipping admission extent differs')
    for b,t,point in zip(p['bounds'],p['tasks'],p['points'],strict=True):
        if b['cell']!=bits(32) or b['margin']!=bits(4) or b['bounds']!=list(map(bits,[-7168,-3072,5120,5120])):
            raise ValueError('point clipping cell/margin/world bounds differ')
        if b['input']!=list(map(bits,t['destination'])) or t['range']!=0 or t['eventCode']!=852331:
            raise ValueError('public point task must retain requested coordinates')
        if engine:
            words=b['input']+b['bounds']+[b['cell']];out=(ctypes.c_uint32*4)()
            engine.pathing_point_order_clip((ctypes.c_uint32*7)(*words),out)
            if list(out)[2:]!=list(map(bits,point)):raise ValueError('production point clipping differs from original admission')
    if spec['name']=='west' and (p['tasks'][0]['destination']!=[-7400,-976] or p['points'][0]!=[4,65.5]):
        raise ValueError('outside click and clipped fine destination were conflated')
    if len(p['markers'])!=(304 if spec['name']=='west' else 303) or not p['markers'][-1].endswith('order=0'):
        raise ValueError('point clipping producer did not naturally finish')
    if spec['name']=='west':
        if sum('label=outside_order_accepted ' in m for m in p['markers'])!=1 or len(spec['motion'])!=191:
            raise ValueError('outside point public order/motion extent differs')
        retry=[r for r in spec['lifecycle'] if r['event']=='retry-result']
        if [(r['counter'],r['before'],r['after'],r['result']) for r in retry]!=[(1246,0,1,1),(1247,1,1,4)]:
            raise ValueError('outside point retry lifecycle differs')
    else:
        expected=[]
        for i in range(12):expected.extend([f'PATHBOUND case={i} before',f'PATHBOUND case={i} accepted'])
        if p['public']!=expected:raise ValueError('edge neighbors public order admission differs')


def render_header(fixture):
    return ('/* Literal original public outside-west point Move. */\nstatic uint32_t const outside_west_motion[][7]={\n'+
        ''.join('    {'+','.join(str(v)+'u' for v in r)+'},\n' for r in fixture['journeys']['west']['motion'])+'};\n')


def verify_capture(rows,fixture,case,engine):
    spec=fixture['journeys'][case['journey']];verify_producer(spec,engine)
    metadata=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or {k:v for k,v in metadata[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('point clipping provenance differs')
    if len(ends)!=1 or not ends[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('point clipping capture failed/incomplete')
    if producer(rows)!=spec['producer']:raise ValueError('point clipping literal public producer differs')
    for event,count in spec['event_counts'].items():
        if sum(r.get('event')==event for r in rows)!=count:raise ValueError('point clipping event extent differs: '+event)
        if event in ends[0]['counts'] and ends[0]['counts'][event]!=count:raise ValueError('point clipping closing counts differ')
    result=dict(passed=True,public_point_admissions=len(spec['producer']['points']))
    if spec['name']=='west':
        if (motion_words(rows)!=spec['motion'] or digest(canonical(rows))!=spec['phases_sha256'] or
            owner_order(rows)!=spec['owner_order'] or lifecycle(rows)!=spec['lifecycle']):
            raise ValueError('outside point motion/owner/retry/cleanup words differ')
        result.update(verify_motion(rows,engine,None));result.update(verify_primary(rows,engine,spec))
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    engine.pathing_point_order_clip.argtypes=[ctypes.POINTER(ctypes.c_uint32)]*2;results=[]
    for path,case in zip(a.traces,fixture['cases'],strict=True):
        rows=[json.loads(l) for l in path.read_text().splitlines()];result=verify_capture(rows,fixture,case,engine)
        result['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(fixture):raise ValueError('outside point C header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=fixture['scope'])
    for key in ('public_point_admissions','exact_velocity_commits','exact_decisions','owner_callbacks'):report[key]=sum(r.get(key,0) for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
