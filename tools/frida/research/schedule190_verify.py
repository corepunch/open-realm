#!/usr/bin/env python3
"""Verify complete populated owner phases against repeats and a public control."""
import argparse
import hashlib
import json
from pathlib import Path
from follow187_engine_fixture import capture

PHASES={'scheduler','publish','radius','group','decide','commit','settle','separate'}


def timeline(rows):
    visits=[];current=None
    for row in rows:
        event=row['event']
        if event=='owner-begin':
            if current is not None:raise ValueError('nested owner')
            current=[]
        elif event=='owner-end':
            if current is None:raise ValueError('owner end without begin')
            if not current or current[0]['event']!='scheduler':raise ValueError('scheduler must precede all consumers')
            if any(r['c']!=row['c']for r in current):raise ValueError('phase escaped owner tick')
            # All publication and radius collection precede every physical group;
            # pose settling precedes the alternating separation visit.
            coarse=[{'scheduler':0,'publish':1,'radius':2,'group':3,'decide':3,'commit':3,'settle':4,'separate':5}[r['event']]for r in current]
            if coarse!=sorted(coarse):raise ValueError('owner phase order differs')
            group_id=None;decided=False
            for phase in current:
                if phase['event']=='group':group_id=phase['id'];decided=False
                elif phase['event']=='decide':
                    if phase['id']!=group_id or decided:raise ValueError('decision escaped its physical group')
                    decided=True
                elif phase['event']=='commit':
                    if phase['id']!=group_id or not decided:raise ValueError('commit before group decisions')
                    decided=False
            groups=[r for r in current if r['event']=='group']
            if any(a['id'][1]<=b['id'][1]for a,b in zip(groups,groups[1:])):raise ValueError('physical groups not newest first')
            radii=[r for r in current if r['event']=='radius']
            if any(a['id'][1]<=b['id'][1]for a,b in zip(radii,radii[1:])):raise ValueError('radius collection not newest first')
            for group in groups:
                if group['shared'] is None:continue
                shared=[r for r in radii if r['shared']==group['shared']]
                if not shared:raise ValueError('group saw unpublished radius')
                if group['parameters'][2]<max(r['parameters'][2]for r in shared):raise ValueError('radius aggregate rolled back')
            if sum(r['event']=='separate'for r in current)!=1:raise ValueError('separation parity visit absent')
            visits.append(current);current=None
        elif event in PHASES:
            if current is None:raise ValueError('phase without owner')
            current.append(row)
    if current is not None or len(visits)!=1000:raise ValueError('owner sequence incomplete')
    if sum(sum(r['event']=='radius'for r in p)>=2 for p in visits)!=32:raise ValueError('two shared groups missing')
    # Pin both all-group prepublication and the second group's same-tick read.
    first=next(p for p in visits if sum(r['event']=='radius'for r in p)>=2)
    radius=[r for r in first if r['event']=='radius'];groups=[r for r in first if r['event']=='group'and r['shared']is not None]
    if [r['count']for r in radius]!=[1,12]or [r['parameters'][2]for r in radius]!=[0,0x3ffc0000]:
        raise ValueError('shared singleton-to-twelve radius flow differs')
    if [r['parameters'][2]for r in groups]!=[0x3ffc0000,0x3ffc0000]:raise ValueError('both commits must see aggregate')
    if groups[0]['parameters'][1]!=0x7f7fffff or groups[1]['parameters'][1]!=0x40960000:
        raise ValueError('second group must see first group speed accumulation')
    return visits


def verify(expected,archive):
    observed=[];public=[]
    for name,pin in expected['captures'].items():
        rows,markers,mode=capture(archive/name,pin,expected['binary_sha256'],'PATHTRACE ',304)
        public.append(markers)
        if mode=='observe':observed.append(timeline(rows))
    if len(observed)!=2 or observed[0]!=observed[1]:raise ValueError('complete repeated owner phases differ')
    if len(public)!=3 or any(p!=public[0]for p in public):raise ValueError('observer perturbed public timeline')
    normalized=json.dumps(observed[0],separators=(',',':')).encode()
    if hashlib.sha256(normalized).hexdigest()!=expected['timeline_sha256']:raise ValueError('frozen complete phases differ')
    return dict(status='live-populated-owner-order',passed=True,owner_ticks=1000,two_group_ticks=32,
                shared_publications=328,radius_visits=357,separation_visits=1000,public_markers=304,observations=2,controls=1)


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--expected',type=Path,required=True)
    ap.add_argument('--archive',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args();report=verify(json.loads(a.expected.read_text()),a.archive)
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
