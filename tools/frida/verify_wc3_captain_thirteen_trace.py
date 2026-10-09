#!/usr/bin/env python3
"""Verify complete thirteen-recruit captain batches, complete motion and footprint ownership."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_blocked_goal_trace import lifecycle
from verify_wc3_captain_home_trace import producer
from verify_wc3_captain_pair_trace import admission
from verify_wc3_captain_range_trace import range_timeline
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_public_pair_trace import canonical
from verify_wc3_selected_point_trace import digest


def physical_motion(rows):
    pubs=[r for r in rows if r.get('event')=='movement-mask-publication' and
          r['rawcode']==1751543663 and r['category']==202]
    if len(pubs)!=26 or any((pubs[i]['mover'],pubs[i]['identity'])!=(pubs[i+1]['mover'],pubs[i+1]['identity']) for i in range(0,26,2)):
        raise ValueError('captain thirteen physical birth extent differs')
    movers=[r['mover'] for r in pubs[::2]]
    if len(set(movers))!=13 or len({tuple(r['identity']) for r in pubs[::2]})!=13:
        raise ValueError('captain thirteen physical births alias')
    return [[movers.index(r['mover']),*[r['after'][i] for i in (0,2,3,4,5,7)]]
            for r in rows if r.get('event')=='velocity-commit' and r['mover'] in movers]


def footprints(rows):
    movers=[]
    for r in rows:
        if r.get('event')=='movement-mask-publication' and r['rawcode']==1751543663 and r['mover'] not in movers:movers.append(r['mover'])
    return [[r['counter'],r['sharedRadius'],r['footprint'],
             [[movers.index(m['resolved']),m['radius']] for m in r['members']]]
            for r in rows if r.get('event')=='group-footprint-state' and all(m['resolved'] in movers for m in r['members'])]


def verify_contract(f):
    p=f['producer']
    if (p['markers']!=['PATHCAPTAIN home begin','PATHCAPTAIN home before recruit',
                      'PATHCAPTAIN home accepted','PATHCAPTAIN thirteen roster thirteen'] or
        p['recruits']!=[[13,1751543663]] or len(p['samples'])!=304 or
        not p['samples'][-1].endswith('order=0') or
        [sum(r[0]==i for r in f['motion']) for i in range(13)]!=[231,222,290,269,248,239,225,413,236,247,362,353,312] or len(f['motion'])!=3647):
        raise ValueError('captain thirteen public recruitment/complete motion differs')
    expected=[]
    for i in range(13):
        before=[13,13,13,i,0,0]
        expected.append(['captain-range-enter-begin',0x3ffffff8,before,None])
        if i==12:
            for j in range(13):
                expected.extend([['captain-prepare-begin',[3304194048,3272605696],'0x0',j//12,j%12,1,1,0],
                                 ['captain-prepare-end',(j+1)//12,(j+1)%12,j//12]])
        expected.append(['captain-range-enter-end',0x3ffffff8,before,[13,13,13,i+1,0,0]])
    if f['admission']!=expected:
        raise ValueError('captain thirteen all-entered 12+1 batches differ')
    t=f['terrain_support'];levels=[v for n,v in t['level_runs'] for _ in range(n)]
    if (t['width']!=97 or t['height']!=65 or t['origin']!=[-7168,-3072] or
        len(levels)!=97*65 or any(v<0 or v>15 for v in levels) or
        hashlib.sha256(bytes(levels)).hexdigest()!=t['levels_sha256']):
        raise ValueError('captain thirteen W3E support-level geometry differs')
    retry=[r for r in f['lifecycle'] if r['event']=='retry-result']
    if len(retry)!=57 or sum(r['result']==4 for r in retry)!=4:
        raise ValueError('captain thirteen complete retries differ')
    private=[r for r in f['footprints'] if r[1] is None]
    shared=[r for r in f['footprints'] if r[1] is not None]
    if len(private)!=429 or len(shared)!=659 or any(len(r[3])!=1 for r in private):
        raise ValueError('captain thirteen footprint lifetime extent differs')
    if any(r[1]!=1064828928 or r[2]!=1064828928 for r in shared):
        raise ValueError('captain thirteen shared 31/32 footprint differs')
    if [m[0] for m in shared[0][3]]!=[12] or [m[0] for m in shared[1][3]]!=list(range(12)):
        raise ValueError('captain thirteen physical batch creation/owner order differs')


def render_header(f):
    return ('/* Complete literal stationary thirteen-recruit captain journey. */\n'
            'static uint32_t const captain_thirteen_motion[][7]={\n'+
            ''.join('    {'+','.join(str(v)+'u' for v in r)+'},\n' for r in f['motion'])+'};\n'+
            '/* Original W3E support levels used by public point placement. */\n'
            'static uint16_t const captain_thirteen_terrain_levels[][2]={\n'+
            ''.join('    {'+','.join(str(v) for v in row)+'},\n' for row in f['terrain_support']['level_runs'])+'};\n')


def verify_capture(rows,f,case,engine):
    verify_contract(f)
    meta=[r for r in rows if r.get('event')=='metadata'];end=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('captain thirteen provenance differs')
    if len(end)!=1 or not end[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('captain thirteen capture failed/incomplete')
    for e,count in f['event_counts'].items():
        if sum(r.get('event')==e for r in rows)!=count or (e in end[0]['counts'] and end[0]['counts'][e]!=count):
            raise ValueError('captain thirteen event extent differs: '+e)
    if (physical_motion(rows)!=f['motion'] or producer(rows)!=f['producer'] or
        admission(rows)!=f['admission'] or range_timeline(rows)!=f['range_timeline'] or
        lifecycle(rows)!=f['lifecycle'] or footprints(rows)!=f['footprints'] or digest(canonical(rows))!=f['phases_sha256']):
        raise ValueError('captain thirteen complete motion/range/admission/lifecycle differs')
    result=verify_motion(rows,engine,None);result.update(verify_primary(rows,engine,f))
    result.update(recruit_commits=3647,range_enters=13,shared_point_batches=2,retries=57,shared_footprint_updates=659)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    f=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    results=[]
    for path,case in zip(a.traces,f['cases'],strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=case['trace_sha256'] or path.stat().st_size!=case['bytes']:
            raise ValueError('captain thirteen trace hash/extent differs')
        results.append(verify_capture([json.loads(l) for l in path.read_text().splitlines()],f,case,engine))
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(f):
        raise ValueError('captain thirteen literal engine reference differs')
    report=dict(passed=True,cases=len(results),results=results,scope=f['scope'],
                recruit_commits=sum(r['recruit_commits'] for r in results),owner_callbacks=sum(r['owner_callbacks'] for r in results))
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
