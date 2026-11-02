#!/usr/bin/env python3
"""Verify native captain range producers; broader movement/lifecycles remain open."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path


def range_state(rows):
    births=[]; seen=set(); current={}; arrival={}; maximum={}; authored=[]; slots=[]; ticks=[]
    for r in rows:
        event=r.get('event')
        if event=='movement-mask-publication' and r.get('category')==202 and 1749242416<=r.get('rawcode',0)<=1749242421:
            key=tuple(r['identity'])
            if key not in seen:
                seen.add(key);current[r['mover']]=len(births);births.append(r['rawcode'])
        elif event=='arrival-evaluation' and r['mover'] in current:
            arrival.setdefault(current[r['mover']],[r['footprint'],r['storedRange']])
        elif event=='captain-max-attack-range':maximum[r['attack']]=r['range']
        elif event=='captain-authored-follow-range':
            if r['attack'] not in maximum:raise ValueError('missing enabled maximum')
            authored.append([r['rawcode'],r['aiFlags']&32,r['unitFlags']&0x40000000,maximum[r['attack']],r['range']])
        elif event=='captain-long-range-slot':
            slots.append([r['attackTypes'],r['weaponTypes'],r['ranges'],r['threshold'],r['flags']&0x180000,r['result']])
        elif event=='marker' and r.get('value','').startswith('PATHTRACE tick='):
            ticks.append(int(r['value'].split()[1][5:]))
    if ticks!=list(range(101)):raise ValueError('missing complete public producer samples')
    if len(births)!=7 or len(arrival)!=7 or len(authored)!=7:raise ValueError('range producer extent differs')
    if not any(r.get('event')=='metadata-marker' and r.get('value')=='PATHMETA complete' for r in rows):raise ValueError('incomplete producer')
    if rows[-1].get('event')!='trace-end' or not rows[-1].get('installed') or any(r.get('event')=='trace-failed' or r.get('type')=='error' for r in rows):raise ValueError('observer failed')
    return dict(births=births,authored=authored,initial_arrivals=[arrival[i] for i in range(7)],slots=slots)


def verify_contract(f):
    s=f['state']
    expected=[[1749242416,0,0,1142276096,1138144051],[1749242417,0,0,1142292480,1138163712],
              [1749242418,32,0,1142308864,1142793830],[1749242419,32,0,1143930880,1143767040],
              [1749242420,32,0,0,1132920832],[1749242421,32,0,1143930880,1143767040],
              [1749242421,0,0,1143930880,1140129792]]
    if s['authored']!=expected:raise ValueError('enabled maximum, retained siege bonus or disabled Attack fallback differs')
    if s['births']!=[1749242416+i for i in range(6)]+[1749242421]:raise ValueError('producer births differ')
    if s['initial_arrivals']!=[[1064828928,v] for v in [1097216819,1097236480,1101358694,1102331904,1091993600,1102331904,1099055104]]:raise ValueError('physical range retention differs')
    if len(s['slots'])!=20 or any(r[3]!=0x44160000 for r in s['slots']):raise ValueError('siege boundary witness differs')
    if hashlib.sha256(json.dumps(s['slots'],separators=(',',':')).encode()).hexdigest()!='3afd8597098f6d40c9b8f2265cb2899645688ac8daff864ac2eb829010ee4404':raise ValueError('complete roster query sequence differs')
    # Both sides of strict >600, delivery controls, and disabled siege must occur.
    required=[([3,1],[2,0],[v,0],0x44160000,0x80000,result) for v,result in [(0x4415c000,0),(0x44160000,0),(0x44164000,1)]]
    required+=[([3,1],[2,0],[0x442f0000,0],0x44160000,0,1),([1,1],[1,0],[0x442f0000,0],0x44160000,0x80000,0),([1,1],[4,0],[0x442f0000,0],0x44160000,0x80000,0)]
    if any(list(r) not in s['slots'] for r in required):raise ValueError('missing public boundary/control')
    if f['whole_retail_pathfinder'] is not False or f['whole_movement_journey'] is not False:raise ValueError('scope overclaim')
    return dict(authored_ranges=7,physical_ranges=7,roster_queries=20,passed=True)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--capture',type=Path,action='append');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    f=json.loads(a.fixture.read_text());result=verify_contract(f)
    for path in a.capture or []:
        data=gzip.decompress(path.read_bytes()) if path.suffix=='.gz' else path.read_bytes()
        rows=[json.loads(x)for x in data.splitlines()]
        if range_state(rows)!=f['state']:raise ValueError('complete native range producer differs')
        if rows[0]['source_sha256']!=f['source_sha256']:raise ValueError('producer provenance differs')
    result['captures']=len(a.capture or []);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
