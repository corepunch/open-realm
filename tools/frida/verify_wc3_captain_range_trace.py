#!/usr/bin/env python3
"""Verify stationary captain range timers, retained occupancy and complete journeys."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_captain_home_trace import recruit_motion, producer, verify_producer
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_public_pair_trace import canonical
from verify_wc3_selected_point_trace import digest


def range_timeline(rows):
    result = []
    for r in rows:
        e = r.get('event')
        if e == 'captain-range-update':
            b, a = r['before'], r['after']
            result.append([e, b['eventCode'], r['clock'][0], r['flag'], b['radius'], b['period'],
                           b['count'], a['count'], b['request'][1], r['counter']])
        elif e in ('captain-range-enter-begin', 'captain-range-enter-end'):
            result.append([e, r['clock'][0], r['counts'], r.get('countsAfter'), r['packet'][2], r['counter']])
    return result


def verify_ranges(rows, spec):
    roster = [r for r in rows if r.get('event') == 'captain-roster-ranges-begin']
    if len(roster) != 1 or roster[0]['counts'] != [0]*6 or roster[0]['delta'] != 1:
        raise ValueError('captain actual roster transition differs')
    if roster[0]['globals'] != [1103626240,1145569280,1128792064,1137180672,1128792064]:
        raise ValueError('captain initialized range constants differ')
    if roster[0]['clock'][0] != 0x3f7ffff0:
        raise ValueError('captain creation phase differs')
    publishes = [r for r in rows if r.get('event') == 'captain-range-publish']
    expected = [[852394,1137590272,1095647232,1056964608,1069547512],
                [852425,1145978880,1104035840,1065353216,1073741816],
                [852426,1149247488,1107304448,1065353216,1073741816],
                [852395,1150885888,1108942848,1056964608,1069547512]]
    actual = [[r['after']['eventCode'],r['requested'],r['after']['radius'],r['after']['period'],
               r['after']['request'][1]] for r in publishes]
    if actual != expected:
        raise ValueError('captain world/fine radii, periods or initial deadlines differ')
    begins = [r for r in rows if r.get('event') == 'captain-range-enter-begin']
    ends = [r for r in rows if r.get('event') == 'captain-range-enter-end']
    if len(begins) != 1 or len(ends) != 1 or begins[0]['clock'][0] != spec['handoff_clock']:
        raise ValueError('captain membership deadline differs')
    if begins[0]['counts'][3] != 0 or ends[0]['countsAfter'][3] != 1:
        raise ValueError('captain enter callback did not admit member')
    timeline = range_timeline(rows)
    if len(timeline) != 172 or digest(timeline) != spec['range_timeline_sha256']:
        raise ValueError('captain full range timeline differs')
    return dict(range_updates=170,range_enters=1,handoff_clock=spec['handoff_clock'])


def verify_virtual_blocker(rows):
    searches=[r for r in rows if r.get('event')=='search' and r.get('kind')=='fine']
    if len(searches)!=3 or [r['pops'] for r in searches]!=[26,701,701]:
        raise ValueError('captain initial/private/retry search extents differ')
    if [r['blockers']['objectHits'] for r in searches]!=[0,12,12]:
        raise ValueError('captain target exclusion or private occupancy differs')
    owners={r['after']['owner'] for r in rows if r.get('event')=='captain-range-publish'}
    if len(owners)!=1:raise ValueError('captain range owner differs')
    owner=owners.pop()
    for r in searches[1:]:
        objects=list(r['blockers']['objects'].values())
        if len(objects)!=1:raise ValueError('captain point route has unexpected blockers')
        o=objects[0]
        if (o['payload']!=owner or not o['isMover'] or o['hits']!=12 or
            o['position']!=[163.5,91.5] or o['cell']!=[163,91] or o['flags']!=0 or
            o['objectMask']!=0x01000002 or o['queryMask']!=0x02000002 or o['mode']!=0):
            raise ValueError('retained zero-radius captain occupancy differs')
    return dict(virtual_blocker_controls=1)


def render_far_header(fixture):
    return ('/* Complete literal original farther-start stationary captain home journey. */\n'
            'static uint32_t const captain_range_far_motion[][7]={\n'+
            ''.join('    {'+','.join(str(v)+'u' for v in row)+'},\n' for row in fixture['journeys']['far']['motion'])+'};\n')


def verify_capture(rows, fixture, case, engine):
    spec = fixture['journeys'][case['journey']]
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or {k:v for k,v in metadata[0].items() if k not in ('event','pid')} != case['metadata']:
        raise ValueError('captain range provenance differs')
    if len(ends) != 1 or not ends[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('captain range capture failed/incomplete')
    for event in ('velocity-commit','motion-decision'):
        if sum(r.get('event') == event for r in rows) != ends[0]['counts'].get(event):
            raise ValueError('captain observer count differs: '+event)
    verify_producer(spec)
    if producer(rows) != spec['producer'] or recruit_motion(rows) != spec['motion']:
        raise ValueError('captain complete physical journey differs')
    if digest(canonical(rows)) != spec['phases_sha256']:
        raise ValueError('captain complete physical/virtual phase words differ')
    if len(spec['motion']) != (178 if case['journey'] == 'near' else 250):
        raise ValueError('captain complete motion reference extent differs')
    result = verify_ranges(rows,spec)
    result.update(verify_virtual_blocker(rows) if case['metadata']['blockers'] else dict(virtual_blocker_controls=0))
    result.update(verify_motion(rows,engine,None))
    result.update(verify_primary(rows,engine,fixture))
    result.update(recruit_commits=len(spec['motion']),whole_engine_parity=True)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path)
    p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',required=True,type=Path)
    p.add_argument('--report',required=True,type=Path)
    a = p.parse_args();fixture=json.loads(a.fixture.read_text())
    engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    results=[]
    for path,case in zip(a.traces,fixture['cases'],strict=True):
        rows=[json.loads(l) for l in path.read_text().splitlines()]
        if hashlib.sha256(path.read_bytes()).hexdigest()!=case['trace_sha256']:
            raise ValueError('captain range archive bytes differ')
        results.append(verify_capture(rows,fixture,case,engine))
    if a.check_engine_header.read_text()!=render_far_header(fixture):raise ValueError('far captain C reference differs')
    report=dict(passed=True,cases=len(results),results=results,scope=fixture['scope'],whole_engine_parity=True)
    for key in ('exact_velocity_commits','exact_decisions','owner_callbacks','recruit_commits','range_updates','range_enters','virtual_blocker_controls'):
        report[key]=sum(r[key] for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__ == '__main__':main()
