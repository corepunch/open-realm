#!/usr/bin/env python3
"""Repeated, read-only public Way Gate allocation/activation/removal witnesses.

The complete 259-birth producer tail, 256 public active queries and two releases
are required. No portal search or physical trajectory is certified here.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path


def normalize(rows):
    result=[]
    for original in rows:
        if not original.get('event','').startswith('gate-pool-'):continue
        row=copy.deepcopy(original)
        row.pop('ms',None);row.pop('unit',None)
        for key in ('before','after'):
            if key not in row:continue
            used=row[key]['used']
            if len(used)!=256 or any(v not in (0,1)for v in used) or used[0]:
                raise ValueError('invalid availability bitmap')
            # Lossless packing of the original 256 availability bytes (each 0/1).
            row[key]['used']=f'{sum(v<<i for i,v in enumerate(used)):064x}'
        result.append(row)
    return result


def verify(rows,fixture,capture):
    meta=[r for r in rows if r.get('event')=='metadata']
    ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or len(ends)!=1 or not ends[0].get('installed'):
        raise ValueError('incomplete Way Gate capture')
    if any(r.get('event')in ('trace-failed','error')for r in rows):
        raise ValueError('failed Way Gate capture')
    if {k:v for k,v in meta[0].items()if k not in ('event','pid')}!=capture['metadata'] or not meta[0].get('gatePoolEvents'):
        raise ValueError('Way Gate producer provenance differs')
    observed=normalize(rows)
    if observed!=fixture['observations']:raise ValueError('Way Gate pool/public events differ')
    allocated=[r['id']for r in observed if r['event']=='gate-pool-allocate']
    queried=[r['result']for r in observed if r['event']=='gate-pool-active-query']
    released=[r['id']for r in observed if r['event']=='gate-pool-release']
    if allocated!=list(range(1,256))+[0,17,255,0] or queried!=[1]*255+[0] or released!=[17,255]:
        raise ValueError('incomplete public producer tail')
    return observed


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,nargs=2,required=True)
    p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    args=p.parse_args();fixture=json.loads(args.fixture.read_text());observed=[]
    for path,capture in zip(args.capture,fixture['captures']):
        raw=path.read_bytes()
        if len(raw)!=capture['bytes']or hashlib.sha256(raw).hexdigest()!=capture['sha256']:
            raise ValueError('Way Gate capture hash/length differs')
        observed.append(verify([json.loads(r)for r in raw.splitlines()if r.strip()],fixture,capture))
    if observed[0]!=observed[1]:raise ValueError('Way Gate repeats differ')
    args.report.write_text(json.dumps(dict(passed=True,cases=2,observations=len(observed[0]),allocations=259,queries=256,releases=2,whole_retail_pathfinder=False),indent=2)+'\n')


if __name__=='__main__':main()
