#!/usr/bin/env python3
"""Verify the full overlap owner stream against original replay and frozen C inputs."""
import argparse
from collections import Counter,defaultdict
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import verify_wc3_pathing_proximity as proximity

HERE=Path(__file__).resolve().parent
FROZEN_SHA='c6a63ba77761771757bd6a94057c2fd018c46c554f8d4b80e98863af491c8e89'
WITNESS_SHA='436190f8d21bec3f9c0e4e9c82bab3fa734feebfc49e667515d8fcd794b0fd6a'


def c_array(source,name):
    match=re.search(re.escape(name)+r'\[\](?:\[\d+\])?=\{',source)
    if not match:raise ValueError('missing engine array: '+name)
    text=source[match.end():].split('};',1)[0]
    text=re.sub(r'\b(\d+)u\b',r'\1',text).replace('{','[').replace('}',']').strip().rstrip(',')
    return json.loads('['+text+']')


def check_fixture(root,expected,rows):
    source=(HERE.parents[1]/'games/warcraft-3/game/tests/retail_repulsion_overlap.h').read_text()
    sys.path.insert(0,str(HERE.parent/'frida/research'))
    from sep_research_map import UNITS
    units=[]
    for unit in json.loads((root/'map.json').read_text())['roster'][27:]:
        first=next((r for r in rows if r['i']==unit['i']),None)
        pos=first['posBefore'] if first else [struct.unpack('<I',struct.pack('<f',unit[a]/32))[0] for a in ('x','y')]
        data=UNITS[unit['code']]
        units.append([*pos,unit['owner'],data.get('urpp',0),data.get('urpr',0),data.get('urpo',0),int(data.get('ucol',8))])
    if c_array(source,'overlap_units')!=units:raise ValueError('overlap authored input differs')
    state=expected['seed']['ownerAtFirstSeparationVisit'];visits=[];pairs=[]
    for row in rows:
        before=state;neighbors=row.get('steps',[])
        if neighbors:
            if neighbors[0]['ownerBefore']!=state:raise ValueError('discontinuous global owner stream')
            state=neighbors[-1]['ownerAfter']
        ap=row.get('apply');endpoint=ap.get('endpoint') if ap else None;valid=ap.get('valid') if ap else None
        visits.append([row['i'],row['visit'],[*row['posBefore'],*row['vecBefore'],row['wordBefore']],
                       [*row['posAfter'],*row['vecAfter'],row['wordAfter']],row['occAfter'],before,state,
                       endpoint if endpoint is not None else [0,0],valid if valid is not None else -1 if ap else -2])
        pairs.extend([[row['i'],p['j'],*p['source'],*p['candidate'],*p['vecBefore'],*p['vecAfterLive'],*p['ownerBefore'],*p['ownerAfter']] for p in neighbors])
    if c_array(source,'overlap_visits')!=visits:raise ValueError('complete overlap visit fixture differs')
    if c_array(source,'overlap_pairs')!=pairs:raise ValueError('individual overlap contribution fixture differs')
    if state!=expected['drawStream']['finalOwner']:raise ValueError('final shared owner state differs')
    return len(visits),len(pairs)


def verify_overlap(root,captures,verified_rows):
    from sep_research_expected import norm
    raw=gzip.decompress((proximity.FIXTURES/'research/SEP-02.3-expected.json.gz').read_bytes())
    if hashlib.sha256(raw).hexdigest()!=FROZEN_SHA:raise ValueError('changed frozen overlap contract')
    expected=json.loads(raw)
    raw=(proximity.FIXTURES/'research/SEP-04.1-expected.json').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=WITNESS_SHA:raise ValueError('changed complete corner witness')
    witness=json.loads(raw)
    if witness['primary']!=expected['overlapGroups'][-1]:raise ValueError('complete corner witness differs')
    contrast=witness['contrast'];group=expected['overlapGroups'][0]
    if contrast.get('truncated') is not True or len(contrast['sequence'])!=64 or contrast['sequence']!=group['sequence'][:64] or any(
            value!=group.get(key) for key,value in contrast.items() if key not in ('sequence','truncated')):
        raise ValueError('archived centre contrast differs from its exact prefix')
    for cap,all_rows in zip(captures,verified_rows):
        rows=[r for r in all_rows if r['i']>=27]
        groups=[[norm(row) for row in rows if row['i'] in group['units']] for group in expected['overlapGroups']]
        if groups!=[g['sequence'] for g in expected['overlapGroups']]:raise ValueError('complete normalized overlap replay differs')
        live=[r for r in cap.rows if r['event']=='owner-draw']
        startup=[r for r in live if r['inSep'] is None];draws=[r for r in live if r['inSep'] is not None]
        wanted=[[p['ownerBefore'],p['ownerAfter']] for r in all_rows for p in r.get('steps',[]) if p['randomBranch']]
        if [[r['before'],r['after']] for r in draws]!=wanted or len(draws)!=expected['drawStream']['separationDraws']:
            raise ValueError('complete live shared draw order differs')
        if [[r['before'],r['after']] for r in startup]!=expected['seed']['startup']['stream']:
            raise ValueError('recorded startup stream differs')
        if sorted({hex(r['caller']) for r in startup})!=expected['seed']['startup']['callers']:
            raise ValueError('startup consumer differs')
        if live[-1]['after']!=expected['drawStream']['finalOwner']:raise ValueError('live final state differs')
        per=defaultdict(Counter);lookup={u['i']:(u['phase'],u['cluster']) for u in cap.roster}
        for r in all_rows:
            counts=per[lookup[r['i']]]
            if r['kind']!='body':counts['cooldownVisits' if r['kind']=='cooldown' else 'movingVisits']+=1;continue
            counts['bodies']+=1;counts['draws']+=sum(p['randomBranch'] for p in r['steps'])
            ap=r['apply'];counts['accepted' if ap['valid']==1 and ap['moved'] else 'rejected' if ap['valid']==0 else 'noApply']+=1
            if r['occBefore']!=r['occAfter']:counts['occupancyChanges']+=1
            if r['wordAfter']&0xffff==7:counts['cooldownInstalls']+=1
        if {f'P{k[0]}C{k[1]}':dict(v) for k,v in per.items()}!=expected['perCluster']:
            raise ValueError('complete endpoint/occupancy/cooldown outcomes differ')
        visits,pairs=check_fixture(root,expected,rows)
    return dict(overlap_visits=visits,overlap_pairs=pairs,shared_draws=len(draws),overlap_groups=len(groups),
                owner_passes=rows[-1]['visit']-rows[0]['visit']+1,corner_visits=len(witness['primary']['sequence']),
                final_owner=expected['drawStream']['finalOwner'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();result=proximity.verify(args.binary.resolve(),verify_overlap)
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()
