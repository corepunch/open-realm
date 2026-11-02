#!/usr/bin/env python3
"""Verify a producer-built partial adaptive plan through public Move failure."""
import argparse
import collections
import ctypes
import hashlib
import json
import re
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_blocked_goal_trace import lifecycle
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_selected_queued_trace import canonical, digest, motion_words, owner_order


def render_header(spec):
    return ('/* Literal retail size-2 partial-plan public journey. */\n'
        'static uint32_t const passage80_rows[32]={'+','.join(str(v)+'u' for v in spec['input_rows'])+'};\n'
        'static uint32_t const passage80_motion[][7]={\n'+
        ''.join('    {'+','.join(str(v)+'u' for v in row)+'},\n' for row in spec['motion'])+'};\n')


def verify_contract(spec):
    marks=spec['markers']; events=spec['lifecycle']
    parsed=[re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)',m) for m in marks]
    if (any(m is None for m in parsed) or len(parsed)!=304 or
        [int(m[1]) for m in parsed if m[2]=='sample']!=list(range(1,301)) or
        [(int(m[1]),m[2],int(m[5])) for m in parsed if m[2]!='sample']!=
        [(0,'start_adaptive_passage',0),(10,'before_move',0),(10,'after_move',851986),(300,'complete',0)]):
        raise ValueError('passage public producer/admission/completion differs')
    searches=[r for r in events if r['event']=='search']; routes=[r for r in events if r['event']=='route']
    if ([(r['kind'],r['result'],r['pops'],r['budget']) for r in searches]!=
        [('acc',-1,38,5000),('acc',1,3,400),('fine',1,52,700),('acc',1,4,400),
         ('fine',1,225,700),('fine',1,6,700),('acc',-1,32,400),('fine',-1,701,700),('fine',-1,701,700)] or
        [r['count'] for r in routes]!=[6,3,19,4,24,6,1,10,1] or
        any(r['truncated'] or len(r['points'])!=r['count'] for r in routes) or
        any(r['footprint']!=1.25 for r in searches) or
        any(r.get('sizeClass')!=2 for r in searches if r['kind']=='acc')):
        raise ValueError('passage partial plan and fine fallback extent differ')
    if routes[0]['points']!=[[16.5,26.5],[17.25,25.25],[13.25,17.25],[13.25,13.25],[9.25,9.25],[4.25,4.75]]:
        raise ValueError('passage original reduced veto plan differs')
    retries=[r for r in events if r['event']=='retry-result']
    forced=[r for r in events if r['event']=='force-arrival']
    if ([(r['counter'],r['before'],r['after'],r['result']) for r in retries]!=[(1492,0,1,1),(1493,1,1,4)] or
        any(r['ownerBefore']!=r['ownerAfter'] for r in retries) or len(forced)!=1 or
        forced[0]['counter']!=1493 or forced[0]['after']!=65536):
        raise ValueError('passage failure retry/forced arrival differs')
    for event in ('task-arrival','task-recovery','task-cant-path'):
        cleanup=[r for r in events if r['event']==event]
        if len(cleanup)!=1 or cleanup[0]['after']['taskHead']!=[-1,-1] or cleanup[0]['after']['orderHead']!=[-1,-1]:
            raise ValueError('passage task/order cleanup differs')
    if len(spec['motion'])!=437 or spec['motion'][-1][2:]!=[1108907840,1113972512,0,0,1086667245]:
        raise ValueError('passage terminal pose/velocity/facing differs')


def verify(rows,spec,case,engine):
    verify_contract(spec)
    meta=[r for r in rows if r.get('event')=='metadata']; end=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('passage capture provenance differs')
    if len(end)!=1 or not end[0].get('installed') or any(r.get('event')=='trace-failed' or r.get('type')=='error' for r in rows):
        raise ValueError('passage capture failed/incomplete')
    counts=collections.Counter(r.get('event') for r in rows)
    for event,count in spec['event_counts'].items():
        if counts[event]!=count or event in end[0]['counts'] and end[0]['counts'][event]!=count:
            raise ValueError('passage event extent differs: '+event)
    for event,count in counts.items():
        if event.startswith('clock-') and end[0]['counts'].get(event)!=count:
            raise ValueError('passage clock observer did not close: '+event)
    if (digest(canonical(rows))!=spec['phases_sha256'] or motion_words(rows)!=spec['motion'] or
        owner_order(rows)!=spec['owner_order'] or lifecycle(rows)!=spec['lifecycle'] or
        [r['value'] for r in rows if r.get('event')=='marker']!=spec['markers']):
        raise ValueError('passage literal phases/routes/outcome differ')
    loaded=[r for r in rows if r.get('event')=='map-load-complete']
    if len(loaded)!=1:raise ValueError('passage requires one complete real map load')
    loaded=loaded[0]; cells=[215 if spec['input_rows'][y//2]&(1<<(x//2)) else 0 for y in range(64) for x in range(64)]
    if ((loaded['width'],loaded['height'])!=(64,64) or loaded['cells']!=cells or
        loaded['worldBounds']!=[0,0,1157627904,1157627904] or
        digest(loaded['hierarchy'])!=spec['hierarchy_sha256']):
        raise ValueError('passage actual loader terrain/hierarchy differs')
    result=verify_motion(rows,engine,None);result.update(verify_primary(rows,engine,spec))
    result.update(passed=True,searches=9,retries=2,forced_arrivals=1,terminal='failure short of clicked goal',
                  motion_sha256=digest(spec['motion']),hierarchy_sha256=spec['hierarchy_sha256'])
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    spec=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine);results=[]
    engine.pathing_heading_error.argtypes=[ctypes.c_uint32]*3
    engine.pathing_heading_error.restype=ctypes.c_uint32
    for path,case in zip(a.traces,spec['cases'],strict=True):
        rows=[json.loads(l) for l in path.read_text().splitlines()];result=verify(rows,spec,case,engine)
        result['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(spec):raise ValueError('passage engine header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=spec['scope'])
    for key in ('searches','retries','forced_arrivals','exact_decisions','exact_velocity_commits','owner_callbacks'):
        report[key]=sum(r[key] for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
