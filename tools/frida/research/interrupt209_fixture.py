#!/usr/bin/env python3
"""Freeze complete original completion/removal boundaries, retaining identity words."""
from collections import Counter
import json
import re
from pathlib import Path

IGNORED={'metadata','installed','loading-key','trace-end','preload-file','controller-end','control-start-file'}
COUNTS={'marker':152,'owner-begin':400,'owner-end':400,'group-begin':440,'group-end':440,
        'completion-begin':125,'completion-end':125,'finish-member':2,'finished-member':2,
        'remove-tasks-begin':4,'remove-tasks-end':4,'append-order-begin':11,'append-order-end':11,
        'dispatch-order-begin':7,'dispatch-order-end':7,'cancel-successors-begin':15,'cancel-successors-end':15,
        'release-before':206,'release-after':206,'destroy-wrapper':196}


def normalize(rows):
    result=[]
    for row in rows:
        if row['event'] in IGNORED:continue
        row=dict(row)
        if 'member' in row:
            row['member']=row['member'][:]
            row['member'][5]=bool(row['member'][5]) # Only the process address of the resolved mover.
        result.append(row)
    validate(result)
    return result


def validate(rows):
    if dict(Counter(r['event']for r in rows))!=COUNTS:raise ValueError('incomplete boundary stream')
    markers=[(i,r)for i,r in enumerate(rows)if r['event']=='marker']
    def one(label):
        found=[(i,r)for i,r in markers if ' label='+label in r['value']]
        if len(found)!=1:raise ValueError('missing/duplicate '+label)
        return found[0]
    first,before=one('channel-before-mutation');last,after=one('channel-after-mutation')
    c=before['c'];old=before['groups'][0]['id'];new=after['groups'][0]['id']
    if c!=1140 or not before['insideOwner'] or not before['insideGroup'] or after['c']!=c:
        raise ValueError('not a real completion callback')
    if len(before['groups'])!=5 or after['groups'][1:]!=before['groups'] or new==old:
        raise ValueError('premature old owner release/replacement')
    inside=rows[first:last+1]
    accepted=[(i,r)for i,r in enumerate(inside)if r['event']=='marker'and 'label=accepted role='in r['value']]
    ids=[]
    for role,(i,r) in enumerate(accepted):
        match=re.search(r'accepted role=(\d+) order=(\d+)',r['value'])
        if not match or [int(v)for v in match.groups()]!=[role,[851972,851993,851986,851986][role]]:
            raise ValueError('suspended public order differs')
        append=inside[i-1];previous=inside[i-2]
        if append['event']!='append-order-end' or previous['event']!='append-order-begin':
            raise ValueError('command did not use common append admission')
        a,b=previous['unit'],append['unit'];ids.append(b['id'])
        if a['internal']!=b['internal'] or a['count']!=0 or b['count']!=1 or a['head']!=[0xffffffff]*2 or b['head']!=b['tail']:
            raise ValueError('removal task/head was displaced')
    if len(accepted)!=4 or any(r['event']=='dispatch-order-begin'and r['unit']['id']in ids for r in inside):
        raise ValueError('pending order executed or rejected')
    if any(r['event']=='marker'and 'label=issued'in r['value']for r in inside):
        raise ValueError('pending order emitted a public event')
    destroyed=[tuple(r['id'])for r in rows if r['event']=='destroy-wrapper']
    if len(set(destroyed))!=len(destroyed):raise ValueError('duplicate canonical wrapper release')
    for identity in ids:
        hits=[(i,r)for i,r in enumerate(rows)if r['event']=='destroy-wrapper'and r['id']==identity]
        if len(hits)!=1 or hits[0][0]<=last or hits[0][1]['insideOwner']:
            raise ValueError('unit release duplicated or ran inside completion')
    release_first,release_before=one('release-before-mutation')
    release_last,release_after=one('release-after-mutation')
    final=release_after['groups'][0]['id']
    if release_first<=last or release_last<=release_first or any(r['insideOwner'] or r['insideGroup'] or r['c']!=c for r in (release_before,release_after)):
        raise ValueError('release replacement did not run after owner completion')
    if [g['id']for g in release_before['groups']]!=[new,old] or release_after['groups'][1:]!=release_before['groups']:
        raise ValueError('release callback displaced retained owners')
    for counter,expected in [(c,[g['id']for g in before['groups']]),(c+1,[final,new,old])]:
        actual=[r['group']['id']for r in rows if r['event']=='group-begin'and r['c']==counter]
        if actual!=expected:raise ValueError('owner frontier visited a successor early')
        end=next(r for r in rows if r['event']=='owner-end'and r['c']==counter)
        if [g['id']for g in end['groups']]!=([new,old]if counter==c else [final]):
            raise ValueError('empty group did not retire on the following owner visit')
    samples=[r['value']for _,r in markers if 'label=sample'in r['value']]
    if len(samples)!=121 or not all('channel=1 cast=0 effect=0 orders=12'in s for s in samples[15:]):
        raise ValueError('interrupted spell fired again or dropped removal events')
    return c


CONSTANTS='''globals
constant unitstate UNIT_STATE_LIFE=ConvertUnitState(0)
constant unitevent EVENT_UNIT_SPELL_CHANNEL=ConvertUnitEvent(289)
constant unitevent EVENT_UNIT_SPELL_CAST=ConvertUnitEvent(290)
constant unitevent EVENT_UNIT_SPELL_EFFECT=ConvertUnitEvent(291)
endglobals
'''

CHECK='''function I209Check takes nothing returns nothing
call BJassAssert(udg_I209Channel==1 and udg_I209Cast==0 and udg_I209Effect==0,"interrupted arrival never casts twice")
call BJassAssert(GetUnitCurrentOrder(udg_I209Caster)==0 and udg_I209Resumed,"replacement Move completed")
call BJassAssert(udg_I209Orders==12,"each peer retired once, no suspended issued event")
endfunction
'''


def scene(probe):
    source=CONSTANTS+Path(probe).read_text()+'function main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n'+CHECK
    return ('/* Original Interrupt209 public gameplay probe, no synthesized expected values. */\n'
            'static char const interrupt209_scene[] =\n'+
            ''.join('    '+json.dumps(line+'\n')+'\n'for line in source.splitlines())+';\n')
