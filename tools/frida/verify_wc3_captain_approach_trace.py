#!/usr/bin/env python3
"""Verify private captain approach inputs; mixed shared engine continuation stays open."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_primary_clock import verify_primary


def births(rows):
    pubs=[r for r in rows if r.get('event')=='movement-mask-publication' and
          r['category']==202 and r['rawcode'] in (1749240903,1751543663)]
    if len(pubs)!=26 or any(pubs[i]!=dict(pubs[i+1],ms=pubs[i]['ms']) for i in range(0,26,2)):
        raise ValueError('captain approach physical birth extent differs')
    movers=[r['mover'] for r in pubs[::2]]
    if len(set(movers))!=13 or len({tuple(r['identity']) for r in pubs[::2]})!=13 or \
            [r['rawcode'] for r in pubs[::2]]!=[1749240903]+[1751543663]*12:
        raise ValueError('captain approach births alias or differ')
    return movers


def physical_motion(rows):
    movers=births(rows)
    return [[movers.index(r['mover']),*[r['after'][i] for i in (0,2,3,4,5,7)]]
            for r in rows if r.get('event')=='velocity-commit' and r['mover'] in movers]


def authored_ranges(rows):
    units=[]; attacks={}; maxima={}; result=[]
    for r in rows:
        if r.get('event')=='captain-max-attack-range':maxima[r['attack']]=r['range']
        if r.get('event')!='captain-authored-follow-range':continue
        if r['unit'] not in units:units.append(r['unit'])
        if r['unit'] in attacks and attacks[r['unit']]!=r['attack']:
            raise ValueError('captain approach attack identity changed')
        attacks[r['unit']]=r['attack']
        if r['attack'] not in maxima:raise ValueError('captain approach lacks original attack maximum')
        result.append([units.index(r['unit']),r['aiFlags'],r['unitFlags'],r['constants'],
                       maxima[r['attack']],r['range']])
    if len(units)!=13 or len(set(attacks.values()))!=13:
        raise ValueError('captain approach unit/attack extent differs')
    return result


def initial_arrival_ranges(rows):
    movers=births(rows); first={}
    for r in rows:
        if r.get('event')=='arrival-evaluation' and r['mover'] in movers:
            first.setdefault(r['mover'],[r['footprint'],r['storedRange']])
    return [first[m] for m in movers]


def verify_contract(f):
    motion=f['motion']; prefix=[r for r in motion if r[1]<0x41100000]
    if f['event_counts']!={'marker':304,'velocity-commit':5465,'clock-owner-end':1000,
            'captain-authored-follow-range':14,'motion-decision':5194,'arrival-evaluation':5465,
            'route-step':5465,'yield-decision':4334}:
        raise ValueError('captain approach completeness contract differs')
    if len(motion)!=5462 or len(prefix)!=3471 or \
            [sum(r[0]==i for r in motion) for i in range(13)]!=[297,430,379,427,434,459,345,428,438,416,332,398,679]:
        raise ValueError('captain approach complete native motion or engine prefix differs')
    if f['whole_engine_parity'] is not False or f['shared_owner_remains_open'] is not True:
        raise ValueError('captain approach cannot claim whole mixed engine parity')
    constants=[1116471296,1009915459,1060976551,1053609166,1142292480,1133903872]
    expected=[[i,1,517,constants,1119092736,1123549184] for i in range(13)]
    expected.append([1,4097,516,constants,1119092736,1123549184])
    if f['authored_ranges']!=expected:
        raise ValueError('captain approach original authored inputs differ')
    if f['initial_arrival_ranges']!=[[1073479680,1085997056]]+[[1064828928,1083899904]]*12:
        raise ValueError('captain approach physical radii/ranges differ')


def render_header(f):
    return ('/* Complete original mixed13 captain journey; largest mover is birth0. */\n'
            'static uint32_t const captain_thirteen_mixed_motion[][7]={\n'+
            ''.join('    {'+','.join(str(v)+'u' for v in r)+'},\n' for r in f['motion'])+'};\n')


def verify_capture(rows,f,case,engine):
    verify_contract(f)
    meta=[r for r in rows if r.get('event')=='metadata']; end=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or {k:v for k,v in meta[0].items() if k not in ('event','pid')}!=case['metadata']:
        raise ValueError('captain approach provenance differs')
    if len(end)!=1 or not end[0].get('installed') or any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('captain approach capture failed/incomplete')
    for event,count in f['event_counts'].items():
        if sum(r.get('event')==event for r in rows)!=count or (event in end[0]['counts'] and end[0]['counts'][event]!=count):
            raise ValueError('captain approach event extent differs: '+event)
    if physical_motion(rows)!=f['motion'] or authored_ranges(rows)!=f['authored_ranges'] or \
            initial_arrival_ranges(rows)!=f['initial_arrival_ranges']:
        raise ValueError('captain approach original motion/range inputs differ')
    result=verify_motion(rows,engine,None); result.update(verify_primary(rows,engine,f))
    result.update(native_recruit_commits=5462,engine_prefix_commits=3471,authored_range_calls=14)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path); p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path); p.add_argument('--report',required=True,type=Path); a=p.parse_args()
    f=json.loads(a.fixture.read_text()); engine=ctypes.CDLL(str(a.engine_library.resolve())); configure(engine)
    results=[]
    for path,case in zip(a.traces,f['cases'],strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=case['trace_sha256'] or path.stat().st_size!=case['bytes']:
            raise ValueError('captain approach trace hash/extent differs')
        results.append(verify_capture([json.loads(l) for l in path.read_text().splitlines()],f,case,engine))
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(f):
        raise ValueError('captain approach literal engine reference differs')
    report=dict(passed=True,cases=len(results),results=results,scope=f['scope'],whole_engine_parity=False,
                engine_prefix_commits=sum(r['engine_prefix_commits'] for r in results),
                owner_callbacks=sum(r['owner_callbacks'] for r in results))
    a.report.write_text(json.dumps(report,indent=2)+'\n'); print(json.dumps(report))


if __name__=='__main__':main()
