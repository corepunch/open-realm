#!/usr/bin/env python3
"""Verify native shared captain owners; engine parity ends before recovery reentry."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from verify_wc3_arrival_trace import configure
from verify_wc3_captain_approach_trace import births, verify_capture as verify_approach


def shared_state(rows):
    movers=births(rows); owners=[]; footprints=[]
    for r in rows:
        if r.get('event')!='group-footprint-state' or not r.get('sharedIdentity'):continue
        if not all(m['resolved'] in movers for m in r['members']):continue
        identity=r['sharedIdentity']
        if identity not in owners:owners.append(identity)
        if any(m['owner']!=r['identity'] for m in r['members']):
            raise ValueError('shared captain contains a foreign physical member')
        footprints.append([r['counter'],owners.index(identity),
            sum(1<<movers.index(m['resolved']) for m in r['members']),r['sharedRadius'],r['footprint']])
    publication=[[owners.index(r['identity']),r['before'],r['after']] for r in rows
                 if r.get('event')=='captain-shared-publish' and r['identity'] in owners]
    return dict(identities=owners,footprints=footprints,publication=publication)


def verify_contract(f):
    s=f['shared_state']; fp=s['footprints']; pub=s['publication']
    if s['identities']!=[[1471,1941],[1471,2193]] or len(fp)!=353 or len(pub)!=325:
        raise ValueError('shared captain owner generations or lifetime extent differs')
    if f['whole_engine_parity'] is not False or f['recovery_reentry_remains_open'] is not True or \
            f['engine_prefix_commits']!=4560 or f['engine_end_msec']!=12000:
        raise ValueError('shared captain engine scope exceeds verified first phase')
    prefix=[r for r in fp if r[0]<1425]
    if len(prefix)!=127 or prefix[:2]!=[[1325,0,1,0x3ffc0000,0x3ffc0000],
                                       [1325,0,8190,0x3ffc0000,0x3ffc0000]]:
        raise ValueError('shared captain global largest final batch differs')
    transitions=[]
    for r in fp:
        key=[r[1],r[3],r[4]]
        if not transitions or transitions[-1][1:]!=key:transitions.append([r[0],*key])
    if transitions!=[[1325,0,0x3ffc0000,0x3ffc0000],[1352,0,0x3f780000,0x3ffc0000],
                     [1558,1,0x3ffc0000,0x3ffc0000],[1561,1,0x3f780000,0x3ffc0000]]:
        raise ValueError('shared captain live radius and cached footprint were conflated')
    first=[r for r in pub if r[0]==0][:2]
    if first!=[[0,[2,0x7f7fffff,0x7f7fffff,0],[2,0x7f7fffff,0x7f7fffff,0]],
               [0,[2,0x7f7fffff,0x40960000,0x3ffc0000],[2,0x40960000,0x7f7fffff,0]]]:
        raise ValueError('shared captain initial references or speed publication differs')
    for _,before,after in pub:
        if before[0]!=after[0] or after[1]!=before[2] or after[2:]!=[0x7f7fffff,0]:
            raise ValueError('shared captain prior speed publication/reset differs')


def render_header(f):
    return ('/* Original mixed captain shared footprints before the first recovery reentry. */\n'
            'static uint32_t const captain_thirteen_shared_footprints[][4]={\n'+
            ''.join('    {'+','.join(str(v)+'u' for v in [r[0],r[2],r[3],r[4]])+'},\n'
                    for r in f['shared_state']['footprints'] if r[0]<1425)+'};\n')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('traces',nargs='+',type=Path)
    p.add_argument('--fixture',required=True,type=Path);p.add_argument('--engine-library',required=True,type=Path)
    p.add_argument('--check-engine-header',type=Path);p.add_argument('--report',required=True,type=Path);a=p.parse_args()
    f=json.loads(a.fixture.read_text());verify_contract(f)
    approach_path=a.fixture.parent/f['approach_fixture']
    if hashlib.sha256(approach_path.read_bytes()).hexdigest()!=f['approach_sha256']:
        raise ValueError('shared captain approach fixture changed')
    approach=json.loads(approach_path.read_text());engine=ctypes.CDLL(str(a.engine_library.resolve()));configure(engine)
    results=[]
    for path,case in zip(a.traces,approach['cases'],strict=True):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=case['trace_sha256'] or path.stat().st_size!=case['bytes']:
            raise ValueError('shared captain capture provenance differs')
        rows=[json.loads(l) for l in path.read_text().splitlines()]
        result=verify_approach(rows,approach,case,engine)
        if shared_state(rows)!=f['shared_state']:raise ValueError('shared captain literal owner state differs')
        result.update(engine_prefix_commits=4560,shared_footprints=353,shared_publications=325)
        results.append(result)
    if a.check_engine_header and a.check_engine_header.read_text()!=render_header(f):
        raise ValueError('shared captain literal engine footprints differ')
    report=dict(passed=True,cases=len(results),results=results,scope=f['scope'],whole_engine_parity=False)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
