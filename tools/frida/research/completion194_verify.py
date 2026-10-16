#!/usr/bin/env python3
"""Verify real public callback mutation inside retail group completion.

The complete function-entry stream and observer-free public control are frozen.
This does not certify the engine's complete spell/channel timing contract.
"""
import argparse
import hashlib
import json
from pathlib import Path
from follow187_engine_fixture import capture
ROOT=Path(__file__).resolve().parents[3]


def timeline(rows):
    result=[]
    for row in rows:
        if row['event'] in ('metadata','installed','loading-key','trace-end','controller-end','preload-file'):continue
        row=dict(row)
        if 'member' in row:
            row['member']=list(row['member'])
            row['member'][5]=bool(row['member'][5]) # Absolute resolved mover address; canonical identity remains exact.
        result.append(row)
    validate(result)
    return result


def validate(rows):
    markers=[(i,r)for i,r in enumerate(rows)if r['event']=='marker']
    before=[(i,r)for i,r in markers if ' label=channel-before-mutation' in r['value']]
    after=[(i,r)for i,r in markers if ' label=channel-after-mutation' in r['value']]
    if len(before)!=1 or len(after)!=1:raise ValueError('missing/duplicate actual channel mutation')
    i,b=before[0];j,a=after[0];c=b['c']
    if j!=i+1 or a['c']!=c or not all(r['insideOwner']and r['insideGroup']for r in (a,b)):
        raise ValueError('mutation did not execute inside one completion callback')
    if len(b['groups'])!=2 or len(a['groups'])!=3 or a['groups'][1:]!=b['groups']:
        raise ValueError('new owner did not prepend without immediate old-owner release')
    old,peer=(r['id']for r in b['groups']);new=a['groups'][0]['id']
    if new in (old,peer):raise ValueError('owner generation was reused')
    finish=rows[i-1]
    if finish['event']!='finish-member' or not finish['member'][5]:raise ValueError('missing actual mover completion')
    if rows[j+1]['event']!='finished-member' or rows[j+1]['member'][:2]!=[0xffffffff]*2 or rows[j+1]['member'][5]:
        raise ValueError('finished member retained its resolved owner')
    visits=[r['group']['id']for r in rows if r['event']=='group-begin'and r['c']==c]
    if visits!=[old,peer]:raise ValueError('current owner frontier changed during callback')
    end=next(r for r in rows[j:]if r['event']=='owner-end')
    if [g['id']for g in end['groups']]!=[new,old]:raise ValueError('old completed owner released early or removed peer survived its visit')
    visits=[r['group']['id']for r in rows if r['event']=='group-begin'and r['c']==c+1]
    if visits!=[new,old]:raise ValueError('new owner did not wait exactly one visit')
    end=next(r for r in rows[j:]if r['event']=='owner-end'and r['c']==c+1)
    if [g['id']for g in end['groups']]!=[new]:raise ValueError('completed owner survived its next empty visit')
    if sum(r['event']=='owner-begin'for r in rows)!=400 or sum(r['event']=='owner-end'for r in rows)!=400:
        raise ValueError('incomplete owner intervals')
    if sum(r['event']=='finish-member'for r in rows)!=2:raise ValueError('incomplete completion lifetimes')
    return c


def verify(expected,archive):
    observed=[];public=[]
    for name,pin in expected['captures'].items():
        rows,markers,mode=capture(archive/name,pin,expected['binary_sha256'],'C194 ',126)
        public.append(markers)
        if mode=='observe':observed.append(timeline(rows))
    if len(observed)!=2 or observed[0]!=observed[1] or observed[0]!=expected['timeline']:
        raise ValueError('ordered completion streams differ')
    if len(public)!=3 or any(m!=public[0]for m in public):raise ValueError('public control differs')
    for source,digest in expected['sources'].items():
        if hashlib.sha256((ROOT/source).read_bytes()).hexdigest()!=digest:raise ValueError('source differs: '+source)
    if hashlib.sha256((archive/'RS-Completion194b.w3m').read_bytes()).hexdigest()!=expected['map_sha256']:
        raise ValueError('map differs')
    return dict(passed=True,status='live-completion-callback-mutation',observations=2,controls=1,
                public_markers=126,owner_intervals=400,completed_members=2,mutation_counter=validate(observed[0]),
                current_frontier_visits=2,new_owner_delay=1,completed_owner_delay=1)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('expected','archive','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('report must be fresh')
    report=verify(json.loads(a.expected.read_text()),a.archive)
    a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()
