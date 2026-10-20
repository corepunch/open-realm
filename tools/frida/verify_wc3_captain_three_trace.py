#!/usr/bin/env python3
"""Verify complete three-recruit captain motion and retained retry membership."""
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
    if len(pubs)!=6 or any((pubs[i]['mover'],pubs[i]['identity'])!=(pubs[i+1]['mover'],pubs[i+1]['identity']) for i in (0,2,4)):
        raise ValueError('captain three physical birth extent differs')
    movers=[r['mover'] for r in pubs[::2]]
    if len(set(movers))!=3 or len({tuple(r['identity']) for r in pubs[::2]})!=3:
        raise ValueError('captain three physical births alias')
    return [[movers.index(r['mover']),*[r['after'][i] for i in (0,2,3,4,5,7)]]
            for r in rows if r.get('event')=='velocity-commit' and r['mover'] in movers]


def verify_contract(f):
    p=f['producer']
    if (p['markers']!=['PATHCAPTAIN home begin','PATHCAPTAIN home before recruit',
                      'PATHCAPTAIN home accepted','PATHCAPTAIN three roster three'] or
        p['recruits']!=[[3,1751543663]] or len(p['samples'])!=304 or
        not p['samples'][-1].endswith('order=0') or
        [sum(r[0]==i for r in f['motion']) for i in range(3)]!=[190,231,187] or len(f['motion'])!=608):
        raise ValueError('captain three public recruitment/complete motion differs')
    expected=[]
    for i in range(3):
        before=[3,3,3,i,0,0]
        expected.append(['captain-range-enter-begin',0x3ffffff8,before,None])
        if i==2:
            for j in range(3):
                expected.extend([['captain-prepare-begin',[3304194048,3272605696],'0x0',0,j,1,1,0],
                                 ['captain-prepare-end',0,j+1,0]])
        expected.append(['captain-range-enter-end',0x3ffffff8,before,[3,3,3,i+1,0,0]])
    if f['admission']!=expected:
        raise ValueError('captain three all-entered shared batch differs')
    retry=[r for r in f['lifecycle'] if r['event']=='retry-result']
    actual=[[r[k] for k in ('counter','members','before','after','result')] for r in retry]
    if actual!=[[1236,3,0,6,1],[1237,3,6,5,1],[1247,2,5,4,1],
                [1257,1,4,3,1],[1267,1,3,2,1],[1277,1,2,1,1],[1287,1,1,1,4]]:
        raise ValueError('captain three retry budget/member lifetime differs')
    if any(r['ownerBefore']!=retry[0]['ownerAfter'] or r['ownerAfter']!=retry[0]['ownerAfter'] for r in retry[1:]):
        raise ValueError('captain three survivor retry redrew owner random state')
    forced=[r for r in f['lifecycle'] if r['event']=='force-arrival']
    if len(forced)!=1 or forced[0]['counter']!=1287 or forced[0]['before']!=0 or forced[0]['after']!=65536:
        raise ValueError('captain three final forced arrival differs')


def render_header(f):
    return ('/* Complete literal stationary three-member captain journey. */\n'
            'static uint32_t const captain_three_motion[][7]={\n'+
            ''.join('    {'+','.join(str(v)+'u' for v in r)+'},\n' for r in f['motion'])+'};\n')


def verify_capture(rows,f,case,engine):
    verify_contract(f)
    meta=[r for r in rows if r.get('event')=='metadata'];end=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('captain three provenance differs')
    if len(end)!=1 or not end[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('captain three capture failed/incomplete')
    for e,count in f['event_counts'].items():
        if sum(r.get('event')==e for r in rows)!=count or (e in end[0]['counts'] and end[0]['counts'][e]!=count):
            raise ValueError('captain three event extent differs: '+e)
    if (physical_motion(rows)!=f['motion'] or producer(rows)!=f['producer'] or
        admission(rows)!=f['admission'] or range_timeline(rows)!=f['range_timeline'] or
        lifecycle(rows)!=f['lifecycle'] or digest(canonical(rows))!=f['phases_sha256']):
        raise ValueError('captain three complete motion/range/admission/lifecycle differs')
    result=verify_motion(rows,engine,None);result.update(verify_primary(rows,engine,f))
    result.update(recruit_commits=608,range_enters=3,shared_point_batches=1,retries=7)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    f=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    results=[]
    for path,case in zip(a.traces,f['cases'],strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=case['trace_sha256'] or path.stat().st_size!=case['bytes']:
            raise ValueError('captain three trace hash/extent differs')
        results.append(verify_capture([json.loads(l) for l in path.read_text().splitlines()],f,case,engine))
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(f):
        raise ValueError('captain three literal engine reference differs')
    report=dict(passed=True,cases=len(results),results=results,scope=f['scope'],
                recruit_commits=sum(r['recruit_commits'] for r in results),owner_callbacks=sum(r['owner_callbacks'] for r in results))
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
