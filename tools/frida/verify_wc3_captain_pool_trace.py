#!/usr/bin/env python3
"""Verify public owned-pool mutations and complete single-recruit captain travel."""
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


def physical_motion(rows, name):
    pubs=[r for r in rows if r.get('event')=='movement-mask-publication' and
          r['rawcode']==1751543663 and r['category']==202]
    births=3 if name=='reuse' else 2
    if len(pubs)!=2*births or any((pubs[i]['mover'],pubs[i]['identity'])!=(pubs[i+1]['mover'],pubs[i+1]['identity']) for i in range(0,len(pubs),2)):
        raise ValueError('owned-pool physical birth/profile extent differs')
    if len({tuple(r['identity']) for r in pubs[::2]})!=births:
        raise ValueError('owned-pool physical birth generation was reused')
    # Native delayed reuse returns the old mover address with a new generation.
    # Birth observations establish roles; address magnitude is never unit order.
    primary=pubs[-1]['mover'] if name=='reuse' else pubs[0]['mover']
    movers=[primary,pubs[2]['mover']]
    if movers[0]==movers[1]:raise ValueError('owned-pool live identities alias')
    return [[movers.index(r['mover']),*[r['after'][i] for i in (0,2,3,4,5,7)]]
            for r in rows if r.get('event')=='velocity-commit' and r['mover'] in movers]


def verify_contract(spec,name):
    pair=name=='partial';selected=None if pair else 1 if name=='same_owner' else 0
    expected_count={'transfer':178,'same_owner':181,'reuse':179,'partial':369}[name]
    marks=spec['producer']['markers']
    after='PATHCAPTAIN pool after recruit primary=851986 peer=851986' if pair else f'PATHCAPTAIN pool after recruit primary={0 if selected else 851986} peer={851986 if selected else 0}'
    if (spec['selected']!=selected or len(spec['motion'])!=expected_count or
        (not pair and any(r[0]!=selected for r in spec['motion'])) or not marks or marks[-1]!=after or
        marks.count('PATHCAPTAIN pool roster two' if pair else 'PATHCAPTAIN pool roster one')!=1 or
        spec['producer']['recruits']!=([[1,1751543663],[2,1751543663]] if pair else [[1,1751543663]]) or
        len(spec['producer']['samples'])!=304 or not spec['producer']['samples'][-1].endswith('order=0')):
        raise ValueError('owned-pool public recruitment/complete motion differs')
    if name=='reuse' and (marks[0]!='PATHCAPTAIN pool delayed remove' or
                         marks[1]!='PATHCAPTAIN pool before delayed create primary=0 peer=0'):
        raise ValueError('owned-pool delayed removal/recreation differs')
    if spec['physical_births']!=(3 if name=='reuse' else 2):
        raise ValueError('owned-pool physical birth count differs')
    prepares=[r for r in spec['admission'] if r[0]=='captain-prepare-begin']
    expected=[['captain-prepare-begin',[3304194048,3272605696],'0x0',0,i,1,1,0] for i in range(2 if pair else 1)]
    if prepares!=expected:
        raise ValueError('owned-pool singleton point admission differs')
    retry=[r for r in spec['lifecycle'] if r['event']=='retry-result']
    if pair:
        if retry or [sum(r[0]==i for r in spec['motion']) for i in range(2)]!=[185,184] or marks.count('PATHCAPTAIN pool partial accepted')!=1:
            raise ValueError('owned-pool partial AddAssault/shared roster differs')
        enters=[r for r in spec['admission'] if r[0]=='captain-range-enter-end']
        if len(enters)!=2 or [r[3][3] for r in enters]!=[1,2]:
            raise ValueError('owned-pool partial all-entered gate differs')
    elif len(retry)!=2 or [r['result'] for r in retry]!=[1,4] or any(r['members']!=1 for r in retry):
        raise ValueError('owned-pool singleton retry lifecycle differs')


def render_header(fixture):
    return ('/* Complete literal retail captain journeys after owned-pool mutations. */\n'+
        ''.join('static uint32_t const captain_pool_'+name+'_motion[][7]={\n'+
            ''.join('    {'+','.join(str(v)+'u' for v in row)+'},\n' for row in spec['motion'])+'};\n'
            for name,spec in fixture['journeys'].items()))


def verify_capture(rows,fixture,case,engine):
    name=case['journey'];spec=fixture['journeys'][name];verify_contract(spec,name)
    meta=[r for r in rows if r.get('event')=='metadata'];end=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('owned-pool capture provenance differs')
    if len(end)!=1 or not end[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('owned-pool capture failed/incomplete')
    for event,count in spec['event_counts'].items():
        if sum(r.get('event')==event for r in rows)!=count:raise ValueError('owned-pool event extent differs: '+event)
        if event in end[0]['counts'] and end[0]['counts'][event]!=count:raise ValueError('owned-pool closing extent differs: '+event)
    if (physical_motion(rows,name)!=spec['motion'] or producer(rows)!=spec['producer'] or
        admission(rows)!=spec['admission'] or range_timeline(rows)!=spec['range_timeline'] or
        lifecycle(rows)!=spec['lifecycle'] or digest(canonical(rows))!=spec['phases_sha256']):
        raise ValueError('owned-pool complete motion/range/admission/lifecycle differs')
    result=verify_motion(rows,engine,None);result.update(verify_primary(rows,engine,spec))
    result.update(recruit_commits=len(spec['motion']),selected_member=spec['selected'])
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    fixture=json.loads(a.fixture.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    results=[]
    for path,case in zip(a.traces,fixture['cases'],strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=case['trace_sha256']:
            raise ValueError('owned-pool trace hash differs')
        result=verify_capture([json.loads(l) for l in path.read_text().splitlines()],fixture,case,engine)
        results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(fixture):
        raise ValueError('owned-pool literal engine reference differs')
    report=dict(passed=True,cases=len(results),results=results,scope=fixture['scope'],
        recruit_commits=sum(r['recruit_commits'] for r in results),owner_callbacks=sum(r['owner_callbacks'] for r in results))
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
