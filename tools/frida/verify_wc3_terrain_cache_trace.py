#!/usr/bin/env python3
"""Verify fine edits, regional adaptive publication and complete public motion."""
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


def geometry(rows):
    return [{k:v for k,v in r.items() if k!='ms'} for r in rows if r.get('event')=='blocker-geometry']


def producers(rows):
    result=[]
    for r in rows:
        if r.get('event') not in ('search','route','hierarchy-update','terrain-native'):continue
        v={k:v for k,v in r.items() if k not in ('ms','owner','path','system')}
        if isinstance(v.get('request'),dict):v['request']={k:x for k,x in v['request'].items() if k!='path'}
        result.append(v)
    return result


def verify_contract(s):
    labels=['baseline','terrain_edits','first_footprint_insert','first_footprint_remove',
            'second_footprint_insert','second_footprint_remove','complete']
    if [r['marker'] for r in s['geometry']]!=['PATHLIFE label='+x for x in labels]:
        raise ValueError('complete regional publication timeline required')
    for r in s['geometry']:
        if (r['width'],r['height'],r['box'])!=(64,64,[16,16,48,48]) or len(r['masks'])!=1024:
            raise ValueError('fine snapshot shape differs')
        if [(h['level'],h['box'],len(h['values'])) for h in r['hierarchy']]!=[
            (0,[8,8,23,23],256),(1,[4,4,11,11],64),(2,[2,2,5,5],16),(3,[1,1,2,2],4)]:
            raise ValueError('all four regional hierarchy levels required')
    baseline,edited,inserted,first,second_insert,second,complete=s['geometry']
    if any(baseline['masks']) or any(any(v) for h in baseline['hierarchy'] for v in h['values']):
        raise ValueError('original empty loader required')
    expected=[2 if (18<=x<22 and 18<=y<22) or (40<=x<44 and 40<=y<44) else 0
              for y in range(16,48) for x in range(16,48)]
    if edited['masks']!=expected or edited['hierarchy']!=baseline['hierarchy']:
        raise ValueError('fine writes must retain stale hierarchy')
    if any(r['masks']!=expected for r in (first,second,complete)) or second['hierarchy']!=complete['hierarchy']:
        raise ValueError('footprint removal and final retained publication differ')
    for stage,patches in ((first,(18,)),(second,(18,40))):
        for h in stage['hierarchy']:
            # In particular the remote fine patch remains unpublished after the first refresh.
            x=y=40//(2<<h['level']);lo_y,lo_x,hi_y,hi_x=h['box']
            cell=h['values'][(y-lo_y)*(hi_x-lo_x+1)+x-lo_x]
            if (cell[0]!=0) != (40 in patches) or cell[1:]!=[0,0,0]:
                raise ValueError('unrelated terrain must not publish on a footprint refresh')
    marks=[re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)',x) for x in s['markers']]
    if len(marks)!=308 or any(m is None for m in marks) or [int(m[1])for m in marks if m[2]=='sample']!=list(range(1,301)):
        raise ValueError('complete public timer lifetime required')
    if [(int(m[1]),m[2]) for m in marks if m[2]!='sample']!=[
        (0,'start_terrain_cache'),(10,'initial_move'),(20,'terrain_edits'),(85,'reset'),
        (100,'first_refresh_move'),(185,'reset'),(200,'second_refresh_move'),(300,'complete')]:
        raise ValueError('public edit/reset/reissue order differs')
    searches=[r for r in s['producers'] if r['event']=='search']
    if [(r['kind'],r['pops'],r['nodes'],r['result']) for r in searches]!=[
        ('acc',3,9,1),('acc',3,9,1),('fine',59,154,1),('fine',94,156,1),
        ('acc',3,10,1),('acc',3,10,1),('fine',305,327,1),('fine',78,126,1),
        ('acc',3,10,1),('acc',3,10,1),('fine',305,327,1),('fine',78,126,1)]:
        raise ValueError('retained adaptive/local replan search lifetime differs')
    if len(s['motion'])!=724 or digest(s['motion'])!=s['motion_sha256']:
        raise ValueError('complete literal movement required')


def render_header(s):
    out='#ifndef BZ_RETAIL_TERRAIN_CACHE_H\n#define BZ_RETAIL_TERRAIN_CACHE_H\n/* Literal original fine/adaptive publication and complete public Move. */\n'
    out+='static uint32_t const terrain83_motion[][7]={\n'
    out+=''.join('    {'+','.join(str(v)+'u' for v in r)+'},\n' for r in s['motion'])+'};\n'
    out+='static uint8_t const terrain83_fine[7][1024]={\n'
    for r in s['geometry']:out+='    {'+','.join(map(str,r['masks']))+'},\n'
    out+='};\nstatic uint8_t const terrain83_adaptive[7][1360]={\n'
    for r in s['geometry']:out+='    {'+','.join(str(v)for h in r['hierarchy'] for c in h['values'] for v in c)+'},\n'
    return out+'};\n#endif\n'


def verify(rows,s,case,engine):
    verify_contract(s)
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('terrain capture provenance differs')
    if len(ends)!=1 or not ends[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('terrain observer incomplete/failed')
    counts=collections.Counter(r.get('event')for r in rows)
    if dict(counts)!=case['event_counts'] or ends[0]['counts']!=case['observer_counts']:
        raise ValueError('terrain observer record/call extents differ')
    if (geometry(rows)!=s['geometry'] or producers(rows)!=s['producers'] or motion_words(rows)!=s['motion'] or
        owner_order(rows)!=s['owner_order'] or digest(canonical(rows))!=s['phases_sha256'] or
        overlap_lifecycle(rows)!=s['lifecycle'] or [r['value']for r in rows if r.get('event')=='marker']!=s['markers'] or
        [r['destination']for r in rows if r.get('event')=='point-task']!=[[1008,1040]]*3):
        raise ValueError('terrain producers/owned search/motion lifecycle differ')
    loaded=[r for r in rows if r.get('event')=='map-load-complete']
    if len(loaded)!=1 or (loaded[0]['width'],loaded[0]['height'])!=(64,64) or any(loaded[0]['cells']) or digest(loaded[0]['hierarchy'])!=s['hierarchy_sha256']:
        raise ValueError('terrain empty map initialization differs')
    result=verify_motion(rows,engine,None);result.update(verify_primary(rows,engine,s))
    result.update(passed=True,terrain_writes=32,regional_snapshots=7,owned_searches=12,motion_sha256=s['motion_sha256'])
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    s=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    engine.pathing_heading_error.argtypes=[ctypes.c_uint32]*3;engine.pathing_heading_error.restype=ctypes.c_uint32
    results=[]
    for path,case in zip(a.traces,s['cases'],strict=True):
        result=verify([json.loads(l)for l in path.read_text().splitlines()],s,case,engine)
        result['trace_sha256']=hashlib.sha256(path.read_bytes()).hexdigest();results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(s):raise ValueError('terrain literal C header differs')
    report=dict(passed=True,cases=len(results),results=results,scope=s['scope'])
    for key in ('terrain_writes','regional_snapshots','owned_searches','exact_velocity_commits','exact_decisions','owner_callbacks'):
        report[key]=sum(r[key]for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
