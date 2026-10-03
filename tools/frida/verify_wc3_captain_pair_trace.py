#!/usr/bin/env python3
"""Verify a complete stationary captain pair and its all-entered shared admission."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_blocked_goal_trace import lifecycle
from verify_wc3_captain_home_trace import producer
from verify_wc3_captain_range_trace import range_timeline
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary
from verify_wc3_public_pair_trace import canonical
from verify_wc3_selected_point_trace import digest


def recruits(rows):
    movers = []
    for r in rows:
        if r.get('event') == 'movement-mask-publication' and r['rawcode'] in (1751543663,1751871081) and r['category'] == 202:
            if r['mover'] not in movers: movers.append(r['mover'])
    if len(movers) != 2: raise ValueError('captain pair physical identities differ')
    return movers


def motion(rows):
    movers = recruits(rows)
    return [[movers.index(r['mover']), *[r['after'][i] for i in (0,2,3,4,5,7)]]
            for r in rows if r.get('event') == 'velocity-commit' and r['mover'] in movers]


def footprints(rows):
    movers=recruits(rows)
    return [[r['counter'],r['sharedRadius'],r['footprint'],
             [[movers.index(m['resolved']),m['radius']] for m in r['members']]]
            for r in rows if r.get('event')=='group-footprint-state' and
            all(m['resolved'] in movers for m in r['members'])]


def verify_footprints(actual,fixture):
    expected=fixture['footprint_reference']
    if actual != expected:
        raise ValueError('captain pair complete footprint publication differs')
    private=[r for r in actual if r[1] is None]
    shared=[r for r in actual if r[1] is not None]
    policy=fixture.get('footprint_policy',dict(private=66,shared=152,
        first=[[0,1064828928],[1,1064828928]],last=[[0,1064828928]],
        transitions=[[1064828928,1064828928,152]]))
    if len(private)!=policy['private'] or len(shared)!=policy['shared'] or any(len(r[3])!=1 for r in private):
        raise ValueError('captain pair private/shared owner extents differ')
    if shared[0][3]!=policy['first'] or shared[-1][3]!=policy['last']:
        raise ValueError('captain pair shared members/last survivor differ')
    states={}
    for r in shared:states[tuple(r[1:3])]=states.get(tuple(r[1:3]),0)+1
    if [[*k,v] for k,v in states.items()]!=policy['transitions']:
        raise ValueError('captain pair shared7c/pathb4 footprint differs')
    return dict(shared_footprint_updates=len(shared))


def admission(rows):
    """Keep callback nesting and cohort identity; process addresses are not identities."""
    result, shared, requests = [], [], []
    for r in rows:
        e = r.get('event')
        if e in ('captain-range-enter-begin', 'captain-range-enter-end'):
            result.append([e,r['clock'][0],r['counts'],r.get('countsAfter')])
        elif e == 'captain-prepare-begin':
            key=(r['sharedWrapper'],tuple(r['sharedIdentity']))
            if key not in shared: shared.append(key)
            result.append([e,r['point'],r['target'],r['index'],r['count'],r['policy'],r['bindShared'],shared.index(key)])
        elif e == 'captain-prepare-end':
            key=(r['wrapper'],tuple(r['identity']))
            if key not in requests: requests.append(key)
            result.append([e,r['index'],r['count'],requests.index(key)])
    return result


def verify_admission(rows, fixture):
    roster = [r for r in rows if r.get('event') == 'captain-roster-ranges-begin']
    if [r['counts'] for r in roster] != [[0]*6,[1,1,1,0,0,0]] or any(r['delta'] != 1 or r['clock'][0] != 0x3f7ffff0 for r in roster):
        raise ValueError('captain pair actual roster transition differs')
    pubs = [r for r in rows if r.get('event') == 'captain-range-publish']
    words = [[r['after']['eventCode'],r['requested'],r['after']['radius'],r['after']['period'],r['after']['request'][1]] for r in pubs]
    if words != fixture['range_publications']: raise ValueError('captain pair range cardinality/phase differs')
    actual = admission(rows)
    expected = [
        ['captain-range-enter-begin',0x3ffffff8,[2,2,2,0,0,0],None],
        ['captain-range-enter-end',0x3ffffff8,[2,2,2,0,0,0],[2,2,2,1,0,0]],
        ['captain-range-enter-begin',0x3ffffff8,[2,2,2,1,0,0],None],
        ['captain-prepare-begin',[3304194048,3272605696],'0x0',0,0,1,1,0],
        ['captain-prepare-end',0,1,0],
        ['captain-prepare-begin',[3304194048,3272605696],'0x0',0,1,1,1,0],
        ['captain-prepare-end',0,2,0],
        ['captain-range-enter-end',0x3ffffff8,[2,2,2,1,0,0],[2,2,2,2,0,0]],
    ]
    if actual != expected: raise ValueError('captain pair all-entered two-pass admission differs')
    if digest(range_timeline(rows)) != fixture['range_timeline_sha256']:
        raise ValueError('captain pair complete range timeline differs')
    return dict(range_enters=2,shared_point_batches=1)


def verify_retry_lifecycle(rows,fixture):
    if 'lifecycle' not in fixture:return {}
    actual=lifecycle(rows)
    if actual!=fixture['lifecycle']:
        raise ValueError('captain pair complete retry/buffer/cleanup lifecycle differs')
    for event,count in fixture['event_counts'].items():
        if sum(r.get('event')==event for r in rows)!=count:
            raise ValueError('captain pair lifecycle extent differs: '+event)
    retries=[r for r in actual if r['event']=='retry-result']
    initial=[r for r in actual if r['event']=='retry-init']
    forced=[r for r in actual if r['event']=='force-arrival']
    if (len(initial)!=2 or [r['count'] for r in initial]!=[7,7] or
        [r['members'] for r in initial]!=[2,2] or len(retries)!=14 or
        [r['counter'] for r in retries if r['result']==4]!=[1310,1326] or
        len(forced)!=2 or any(r['after']!=65536 for r in forced)):
        raise ValueError('captain pair natural retry/forced arrival contract differs')
    return dict(retries=len(retries),forced_arrivals=len(forced))


def render_header(fixture):
    return ('/* Complete literal original stationary two-recruit home journey; creation-order member indices. */\n'
            'static uint32_t const '+fixture.get('header_symbol','captain_pair_motion')+'[][7]={\n'+
            ''.join('    {'+','.join(str(v)+'u' for v in r)+'},\n' for r in fixture['motion'])+'};\n')


def verify_capture(rows, fixture, case, engine):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ending = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or {k:v for k,v in metadata[0].items() if k not in ('event','pid')} != case['metadata']:
        raise ValueError('captain pair provenance differs')
    if len(ending) != 1 or not ending[0].get('installed') or any(r.get('type') == 'error' or r.get('event') == 'trace-failed' for r in rows):
        raise ValueError('captain pair capture incomplete/failed')
    for e in ('velocity-commit','motion-decision','arrival-evaluation','clock-owner-begin','clock-owner-end'):
        if sum(r.get('event') == e for r in rows) != ending[0]['counts'].get(e):
            raise ValueError('captain pair observer count differs: '+e)
    p = producer(rows)
    if p != fixture['producer'] or p['recruits'] != fixture.get('recruits',[[2,1751543663]]) or p['markers'][-1] != 'PATHCAPTAIN pair roster two':
        raise ValueError('captain pair public AI producer differs')
    actual = motion(rows)
    if actual != fixture['motion'] or [sum(r[0] == i for r in actual) for i in range(2)] != fixture.get('member_commits',[185,184]):
        raise ValueError('captain pair complete physical commits differ')
    if digest(canonical(rows)) != fixture['phases_sha256']:
        raise ValueError('captain pair physical/virtual phases differ')
    result = verify_admission(rows,fixture)
    result.update(verify_footprints(footprints(rows),fixture))
    result.update(verify_retry_lifecycle(rows,fixture))
    result.update(verify_motion(rows,engine,None))
    result.update(verify_primary(rows,engine,fixture))
    result.update(recruit_commits=len(actual))
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
        if hashlib.sha256(path.read_bytes()).hexdigest() != case['trace_sha256']:
            raise ValueError('captain pair archive bytes differ')
        results.append(verify_capture([json.loads(l) for l in path.read_text().splitlines()],fixture,case,engine))
    if a.check_engine_header.read_text() != render_header(fixture): raise ValueError('captain pair C reference differs')
    report=dict(passed=True,cases=len(results),results=results,scope=fixture['scope'])
    for key in ('exact_velocity_commits','exact_decisions','owner_callbacks','recruit_commits','range_enters','shared_point_batches','shared_footprint_updates'):
        report[key]=sum(r[key] for r in results)
    for key in ('retries','forced_arrivals'):
        if any(key in r for r in results):report[key]=sum(r.get(key,0) for r in results)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__ == '__main__': main()
