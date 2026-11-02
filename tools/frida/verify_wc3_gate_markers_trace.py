#!/usr/bin/env python3
"""Full original/live source-marker and three-parent publication witnesses.

Checks all eight stock gate creation/removal snapshots, producer completion and
provenance. Routes and save/load are separate native/engine contracts.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def expected(original):
    result=[]
    for row in original['observations']:
        maps=[];off=0
        for level,side in enumerate((41,20,10,5)):
            n=side*side
            maps.append(dict(width=side,height=side,markers=row['markers']if not level else[0]*n,classes=row['classbytes'][off:off+n]));off+=n
        result.append(dict(event='gate-marker-publish',id=row['identity'],rectangle=list(struct.unpack('<4i',struct.pack('<4f',*row['box']))),maps=maps))
    return result


def verify(rows,fixture,capture,original):
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or len(ends)!=1 or not ends[0].get('installed'):raise ValueError('incomplete gate marker capture')
    if any(r.get('event')in('trace-failed','error')for r in rows):raise ValueError('failed gate marker observer')
    if {k:v for k,v in meta[0].items()if k not in('event','pid')}!=capture['metadata']or not (meta[0].get('gatePoolEvents')or meta[0].get('gateMarkerEvents')):
        raise ValueError('gate marker producer provenance differs')
    completion=[r for r in rows if r.get('event')=='marker'and r.get('value')==fixture['completion']]
    if len(completion)!=1:raise ValueError('gate marker producer did not complete exactly once')
    observed=[{k:v for k,v in r.items()if k!='ms'}for r in rows if r.get('event')=='gate-marker-publish']
    if observed!=expected(original):raise ValueError('live world rectangle/markers/parent classes differ from original')
    return observed


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,nargs=2,required=True)
    p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--original-fixture',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    args=p.parse_args();fixture=json.loads(args.fixture.read_text());raw=args.original_fixture.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=fixture['original_sha256']:raise ValueError('original source fixture changed')
    original=json.loads(raw);observed=[]
    for path,capture in zip(args.capture,fixture['captures']):
        raw=path.read_bytes()
        if len(raw)!=capture['bytes']or hashlib.sha256(raw).hexdigest()!=capture['sha256']:raise ValueError('gate marker capture hash/length differs')
        observed.append(verify([json.loads(r)for r in raw.splitlines()if r.strip()],fixture,capture,original))
    if observed[0]!=observed[1]:raise ValueError('gate marker repeats differ')
    args.report.write_text(json.dumps(dict(passed=True,cases=2,publications=8,hierarchy_cells=2206,whole_retail_pathfinder=False),indent=2)+'\n')


if __name__=='__main__':main()
