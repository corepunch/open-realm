#!/usr/bin/env python3
"""Read-only retail adaptive-table constructor and first-growth witness.

Only these three storage observations are repeated. This is not a trajectory,
node-capacity, allocation-failure or full-map parity claim.
"""
import argparse
import hashlib
import json
from pathlib import Path


def verify(rows, fixture, capture):
    meta = [r for r in rows if r.get('event') == 'metadata']
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if len(meta) != 1 or len(ends) != 1 or not ends[0].get('installed'):
        raise ValueError('incomplete storage capture')
    if any(r.get('event') in ('trace-failed','error') for r in rows):
        raise ValueError('failed storage capture')
    actual = {k:v for k,v in meta[0].items() if k not in ('event','pid')}
    if actual != capture['metadata'] or not actual.get('adaptiveStorageEvents'):
        raise ValueError('storage producer provenance differs')
    selected = [r for r in rows if r.get('event','').startswith('adaptive-storage')]
    constructors = [r for r in selected if r['event'] == 'adaptive-storage-constructed']
    if len(constructors) != 1:
        raise ValueError('adaptive constructor identity missing/duplicated')
    search = int(constructors[0]['search'],16)
    normalized = []
    for row in selected:
        if row['event'] == 'adaptive-storage-constructed':
            normalized.append(dict(event=row['event'],index=row['index'],nodes=row['nodes'],heap=row['heap']))
        else:
            if row['kind'] not in ('nodes','heap') or int(row['table'],16) != search + (0x50 if row['kind']=='nodes' else 0x70):
                raise ValueError('growth belongs to another table')
            normalized.append({k:row[k] for k in ('event','kind','amount','before','result','after')})
    if normalized != fixture['observations']:
        raise ValueError('original constructor/growth words differ')
    return normalized


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,nargs=2,required=True)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args(); fixture=json.loads(args.fixture.read_text())
    result=[]
    for path,capture in zip(args.capture,fixture['captures']):
        raw=path.read_bytes()
        if len(raw)!=capture['bytes'] or hashlib.sha256(raw).hexdigest()!=capture['sha256']:
            raise ValueError('storage capture hash/length differs')
        result.append(verify([json.loads(line) for line in raw.splitlines() if line.strip()],fixture,capture))
    if result[0]!=result[1]:raise ValueError('storage repeats differ')
    args.report.write_text(json.dumps(dict(passed=True,cases=2,observations=3,whole_retail_pathfinder=False),indent=2)+'\n')


if __name__=='__main__':main()
