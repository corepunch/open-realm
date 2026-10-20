#!/usr/bin/env python3
"""Verify public mixed authored-rank installation, buckets and Chaos reinstallation."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_scheduler_trace import HASH

SOURCE_HASHES = {'wc3_formation_rank_probe.j': '100f707c2d54b09217b19f62e3f9d5e7d411ee46772d7a78580433f066215d1e', 'trace_wc3_pathfinding.py': 'aca4bce37b9bc9019846a6e0c9b5ade0d7d7417cb0962603bede396749186596', 'wc3_pathfinding.js': 'aa97626a2596991a24f1faa6f457d6a7d80f1df22f0099c34a0c73d8e796f915', 'make_wc3_pathfinding_map.py': 'd4ee17c7cde919b6a727c70f885b28c5809ba485aaffd87c15aaada3e507a1ec', 'map': '6db18af638ffb8e90c94910deefef9ad616397be2fd73b3bffcec7ed081ec0b7'}
EVENTS = ('formation-authored-rank','formation-rank-set','formation-rank-buckets',
          'formation-rank-layout','formation-rank-marker','formation-rank-center',
          'formation-rank-mean','formation-rank-trig','group-routing-radius','marker')

def verify(rows):
    metadata=[r for r in rows if r.get('event')=='metadata']
    footer=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or metadata[0].get('sha256')!=HASH or not metadata[0].get('profileEvents'):
        raise ValueError('missing pinned rank metadata')
    if any(metadata[0].get('source_sha256',{}).get(n)!=h for n,h in SOURCE_HASHES.items()):
        raise ValueError('rank producer/observer generation differs')
    if len(footer)!=1 or footer[0]!=rows[-1] or not footer[0].get('installed'):
        raise ValueError('incomplete rank capture')
    if any(r.get('event') in ('error','trace-failed') for r in rows):raise ValueError('observer failed')
    counts=Counter(r.get('event') for r in rows)
    for event in EVENTS:
        if event.startswith('formation-') and event!='formation-rank-marker' and counts[event]!=footer[0]['counts'].get(event):
            raise ValueError('truncated rank stream')
    markers=[r['value'] for r in rows if r.get('event')=='marker']
    if [int(re.search(r'tick=(\d+)',v)[1]) for v in markers if 'label=sample ' in v]!=list(range(1,41)):
        raise ValueError('producer samples missing')
    if [v.split('label=')[1].split()[0] for v in markers if 'label=sample ' not in v]!=[
        'start_formation_ranks','created','first_group_before','first_group_after','rebind_before',
        'rebind_return','second_group_before','second_group_after','complete']:
        raise ValueError('producer boundaries missing')
    authored=[r for r in rows if r.get('event')=='formation-authored-rank']
    setters=[r for r in rows if r.get('event')=='formation-rank-set']
    expected=[0,1,2,3,0,1,3,3]
    if [r['rank'] for r in authored]!=expected or [r['input'] for r in setters]!=expected:
        raise ValueError('authored rank installation differs')
    if [r['rawcode'] for r in authored]!=[int.from_bytes(('hF0'+str(i)).encode(),'big') for i in expected]:
        raise ValueError('authored type differs')
    if len({r['mover'] for r in setters[:6]})!=6 or setters[6]['mover']!=setters[0]['mover'] or setters[6]['identity']!=setters[0]['identity'] or setters[7]['mover'] in {r['mover'] for r in setters[:6]}:
        raise ValueError('rebind/fresh mover identity differs')
    for r in setters:
        if r['after']!=((r['before']&0xffff0fff)|((r['input']&255)<<12)):
            raise ValueError('rank setter changed unrelated flags')
    buckets=[r for r in rows if r.get('event')=='formation-rank-buckets']
    layouts=[r for r in rows if r.get('event')=='formation-rank-layout']
    if len(buckets)!=2 or [len(r['before']['members']) for r in layouts]!=[6,1,6]:
        raise ValueError('public layouts missing')
    for index,r in enumerate(buckets):
        ranks=[0,1,2,3,0,1] if index==0 else [3,1,2,3,0,1]
        if len(r['members'])!=6 or [m['mover'] for m in r['members']]!=[s['mover'] for s in setters[:6]]:
            raise ValueError('layout reordered live member identities')
        if [(m['moverFlags']>>12)&15 for m in r['members']]!=ranks or r['ranks']!=4:
            raise ValueError('runtime ranks differ')
        if [(b['rank'],b['count'],b['members']) for b in r['buckets']]!=[
            (rank,ranks.count(rank),[i for i,v in enumerate(ranks) if v==rank]) for rank in range(4)]:
            raise ValueError('rank buckets differ')
        if any(len(b['projected'])!=2*b['count'] for b in r['buckets']):raise ValueError('projection truncated')
        layout=layouts[index*2]
        if layout['before']!={k:v for k,v in r.items() if k not in ('event','ms','heading','ranks','buckets')} or layout['heading']!=r['heading']:
            raise ValueError('bucket/layout inputs differ')
    for r in layouts:
        a,b=r['before'],r['after']
        if {k:v for k,v in a.items() if k!='members'}!={k:v for k,v in b.items() if k!='members'}:
            raise ValueError('layout changed group ownership')
        for x,y in zip(a['members'],b['members']):
            if {k:v for k,v in x.items() if k!='row'}!={k:v for k,v in y.items() if k!='row'} or any(x['row'][i]!=y['row'][i] for i in range(11) if i not in (3,4)):
                raise ValueError('layout changed mover or nonoffset row')
    centers=[r for r in rows if r.get('event')=='formation-rank-center']
    means=[r for r in rows if r.get('event')=='formation-rank-mean']
    if len(centers)!=2 or len(means)!=2:raise ValueError('ordered centering witnesses missing')
    for index,(center,mean) in enumerate(zip(centers,means)):
        layout=layouts[index*2]
        if center['group']!=layout['before']['group'] or mean['group']!=center['group'] or len(center['members'])!=6 or len(mean['mean'])!=2:
            raise ValueError('center belongs to a different group')
        if [m['pose'] for m in center['members']]!=[m['pose'] for m in layout['before']['members']]:
            raise ValueError('centering modified a mover')
    # These are the two calls in each original multi-member layout: projection
    # and rotation. Other captured trig callers are retained in the repeat too.
    trig=[r for r in rows if r.get('event')=='formation-rank-trig' and r['caller'] in (0x16cbb0,0x169e3f)]
    if [r['angle'] for r in trig]!=[v for layout in (layouts[0],layouts[2]) for v in (layout['heading']^0x80000000,layout['heading'])]:
        raise ValueError('projection/rotation witnesses missing')
    rank_markers=[r['value'] for r in rows if r.get('event')=='formation-rank-marker']
    if rank_markers!=['PATHRANK label=rebound type=1749430323','PATHRANK label=fresh_before','PATHRANK label=fresh_after type=1749430323']:
        raise ValueError('public Chaos/fresh type witnesses missing')
    names={}
    def normalize(x,key=None):
        if isinstance(x,dict):return {k:normalize(v,k) for k,v in x.items() if k!='ms'}
        if isinstance(x,list):
            if key=='row':x=list(x);x[5]=hex(x[5])
            return [normalize(v) for v in x]
        if isinstance(x,str) and x.startswith('0x') and x!='0x0':
            if x not in names:names[x]=len(names)
            return names[x]
        return x
    stream=[normalize(r) for r in rows if r.get('event') in EVENTS]
    return dict(events=len(stream),digest=hashlib.sha256(json.dumps(stream,separators=(',',':')).encode()).hexdigest(),stream=stream,
        counts={e:counts[e] for e in EVENTS},authored_ranks=expected,layouts=3,mixed_buckets=2)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--repeat',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--header',type=Path);a=p.parse_args()
    read=lambda path:[json.loads(l) for l in path.read_text().splitlines()]
    result=verify(read(a.capture));repeat=verify(read(a.repeat))
    if result.pop('stream')!=repeat['stream']:raise ValueError('complete ordered rank repeats differ')
    result.update(repeat_verified=True,capture_sha256=hashlib.sha256(a.capture.read_bytes()).hexdigest(),repeat_sha256=hashlib.sha256(a.repeat.read_bytes()).hexdigest(),source_sha256=SOURCE_HASHES)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    if a.header:
        rows=read(a.capture)
        layouts=[r for r in rows if r.get('event')=='formation-rank-layout' and len(r['before']['members'])==6]
        text='/* Original16a5b0 committed inputs/offset outputs; first layout has zero velocity. */\n'
        text+='static uint32_t const formation_rank_heading[2] = {'+','.join(str(r['heading'])+'u' for r in layouts)+'};\n'
        text+='static uint32_t const formation_rank_layout[2][6][6] = {\n'
        for r in layouts:
            text+='    {\n'
            for x,y in zip(r['before']['members'],r['after']['members']):
                radius=next(m['radius'] for event in rows if event.get('event')=='group-routing-radius' and event['group']==r['before']['group'] for m in event['members'] if m['resolved']==x['mover'])
                words=x['pose'][2:4]+[radius,(x['moverFlags']>>12)&15]+y['row'][3:5]
                text+='        {'+','.join(str(v)+'u' for v in words)+'},\n'
            text+='    },\n'
        a.header.write_text(text+'};\n')
if __name__=='__main__':main()
