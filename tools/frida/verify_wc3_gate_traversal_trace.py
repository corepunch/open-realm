#!/usr/bin/env python3
"""Strict completed public cached/fresh retarget and disabled-gate captures.

Only wall time and explicitly identified heap addresses are normalized. All
motion words, requests, route points and consumer results remain exact.
"""
import argparse,hashlib,json
from pathlib import Path
EVENTS={'motion-decision','velocity-commit','route','search','gate-destination','gate-traversal','gate-consumer','marker'}
ADDRESSES={'motion-decision':{'mover'},'velocity-commit':{'mover','fineObject'},'route':{'path'},'search':{'path','system'},'gate-consumer':{'path'},'retry-init':{'path','group','mover'},'retry-result':{'path','group','mover'},'separation-owner':{'mover'},'separation-query':{'source'}}


def semantic(rows,events=EVENTS,movers=None):
    if movers is not None and (not movers or len(set(movers))!=len(movers)):
        raise ValueError('invalid gate motion member mapping')
    result=[]
    for row in rows:
        if row.get('event') not in events:continue
        normalized={k:v for k,v in row.items()if k!='ms'and k not in ADDRESSES.get(row['event'],set())}
        if movers is not None and row['event'] in ('motion-decision','velocity-commit','retry-init','retry-result','separation-owner'):
            if row.get('mover') not in movers:raise ValueError('unknown gate motion member')
            member=movers.index(row['mover'])
            if 'member' in row and row['member']!=member:raise ValueError('gate motion member differs')
            normalized['member']=member
        if movers is not None and row['event']=='separation-query':
            if row.get('source') not in movers or any(member not in movers for member in row['members']):
                raise ValueError('unknown separation query member')
            normalized['source_member']=movers.index(row['source'])
            normalized['members']=[movers.index(member) for member in row['members']]
        result.append(normalized)
    return result


def verify(rows,fixture,capture):
    metadata=[r for r in rows if r.get('event')=='metadata'];end=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or len(end)!=1 or not end[0].get('installed'):raise ValueError('incomplete gate traversal capture')
    if any(r.get('event')in ('error','trace-failed')for r in rows):raise ValueError('failed gate traversal observer')
    if {k:v for k,v in metadata[0].items()if k not in ('event','pid')}!=capture['metadata']:raise ValueError('gate traversal provenance differs')
    if not all(metadata[0].get(k)for k in ('motionEvents','velocityEvents','gatePoolEvents','gateMarkerEvents')):raise ValueError('gate traversal observers not enabled')
    completion=[r for r in rows if r.get('event')=='marker'and r.get('value')==fixture['completion']]
    if len(completion)!=1:raise ValueError('gate traversal producer did not complete exactly once')
    observed=semantic(rows,fixture.get('events',EVENTS),capture.get('motion_movers'))
    if observed!=fixture['observations']:raise ValueError('complete gate traversal words/requests/consumers differ')
    return observed


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path,nargs=2,required=True)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();fixture=json.loads(args.fixture.read_text());results=[]
    for path,capture in zip(args.capture,fixture['captures']):
        raw=path.read_bytes()
        if len(raw)!=capture['bytes']or hashlib.sha256(raw).hexdigest()!=capture['sha256']:raise ValueError('gate traversal capture hash/length differs')
        results.append(verify([json.loads(line)for line in raw.splitlines()if line.strip()],fixture,capture))
    if results[0]!=results[1]:raise ValueError('gate traversal repeats differ')
    args.report.write_text(json.dumps(dict(passed=True,cases=2,counts=fixture['counts'],whole_retail_pathfinder=False),indent=2)+'\n')

if __name__=='__main__':main()
