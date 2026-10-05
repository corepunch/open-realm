#!/usr/bin/env python3
"""Verify read-only retail queue mutation and the next ordered admissions."""
import argparse
from collections import Counter, deque
import hashlib
import json
from pathlib import Path
import re

from verify_wc3_scheduler_trace import HASH, LIMIT, RELOAD

SOURCE_HASHES={
    'wc3_scheduler_mutation_probe.j':'e177b2016d6b495ddc3484d45c1cc0ebfeb2a6882cfe4494e53f74e78b522ce5',
    'trace_wc3_pathfinding.py':'78bc8d3a2558464f4b2eee9f6a3972e125cb04eba9ea035f40f12917e539517c',
    'wc3_pathfinding.js':'b98a850ebbaeda10cf883eb27b83f30caffae4af767008c2f70d1f30d7e89338',
    'make_wc3_pathfinding_map.py':'b7d6050a054f9e3f245c8ad528ffa4eeebd3c0db0cea8e14e57029364b203c08',
    'map':'b990a8e580982e31726a8fb190cd22a21c3a8a1756d8ecad4f37251121850d04',
}
EVENTS=('scheduler-mutation-actor','scheduler-mutation-marker','scheduler-class',
        'scheduler-target','scheduler-admission','scheduler-unlink','scheduler-update',
        'scheduler-acc-request','scheduler-fine-request','search','fine-result','marker')
LABELS=('before_stop','after_stop','after_reissue','after_owner',
        'after_owner_reissue','after_remove','next_tick','final_stopped')


def verify(rows):
    metadata=[r for r in rows if r.get('event')=='metadata']
    footer=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or metadata[0].get('sha256')!=HASH or not metadata[0].get('schedulerEvents') or not metadata[0].get('fineResultEvents'):
        raise ValueError('missing pinned scheduler metadata')
    if any(metadata[0].get('source_sha256',{}).get(n)!=h for n,h in SOURCE_HASHES.items()):
        raise ValueError('mutation producer/observer generation differs')
    if len(footer)!=1 or rows[-1]!=footer[0] or not footer[0].get('installed'):
        raise ValueError('capture is incomplete')
    if any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('observer failed')
    counts=Counter(r.get('event') for r in rows)
    for event in EVENTS:
        if event not in ('search','fine-result','marker') and counts[event]!=footer[0]['counts'].get(event,0):
            raise ValueError('scheduler stream truncated: '+event)
    for kind in ('acc','fine'):
        if sum(r.get('event')=='search' and r.get('kind')==kind for r in rows)!=footer[0]['counts'].get(kind+'-search',0):
            raise ValueError('search stream truncated')
    if counts['fine-result']!=footer[0]['counts'].get('fine-search',0):
        raise ValueError('fine results missing')
    markers=[r['value'] for r in rows if r.get('event')=='marker']
    samples=[int(re.search(r'tick=(\d+)',m)[1]) for m in markers if ' label=sample ' in m]
    if samples!=list(range(1,41)) or sum('tick=40 label=complete ' in m for m in markers)!=1:
        raise ValueError('complete producer timeline missing')
    phases=[m.split('label=')[1].split()[0] for m in markers if ' label=sample ' not in m]
    if phases!=['start_scheduler_mutation','wave_before','wave_after','complete']:
        raise ValueError('producer order differs')
    actors=[r for r in rows if r.get('event')=='scheduler-mutation-actor'][:96]
    if len(actors)!=96 or len({r['path'] for r in actors})!=96 or any(r['mode']!=1 or any(r[k] in ('0x0','0xffffffff') for k in ('unit','mover','path')) for r in actors):
        raise ValueError('initial public actor bindings missing')
    actor_paths={r['path']:i for i,r in enumerate(actors)}
    snapshots=[r for r in rows if r.get('event')=='scheduler-mutation-marker']
    if [r['value'].split('label=')[1] for r in snapshots]!=list(LABELS):
        raise ValueError('mutation boundaries missing')
    names={};stream=[];searches={};fine_results=deque(r for r in rows if r.get('event')=='fine-result');admissions={};next_head=None;mutations=[]
    def normalize(x,key=None):
        if isinstance(x,dict):return {k:normalize(v,k) for k,v in x.items() if k not in ('ms','system')}
        if isinstance(x,list):
            if key=='links':return [normalize(hex(v)) for v in x]
            return [normalize(v) for v in x]
        if isinstance(x,str) and x.startswith('0x') and x not in ('0x0','0xffffffff'):
            if x not in names:names[x]=len(names)
            return names[x]
        return x
    def state(s,kind):
        q=s['queue']
        if s['limit']!=LIMIT[kind] or len(q)!=len(set(q)) or s['head']!=(q[0] if q else '0x0') or s['tail']!=(q[-1] if q else '0x0'):
            raise ValueError('invalid FIFO or work limit')
    for index,r in enumerate(rows):
        event=r.get('event')
        if event not in EVENTS:continue
        stream.append(normalize(r))
        if event=='scheduler-mutation-marker':
            if [s['offset'] for s in r['buckets']]!=list(range(0,224,28)):
                raise ValueError('mutation snapshot misses a player/policy')
            for s in r['buckets']:state(s,(s['offset']%112)//28)
            label=r['value'].split('label=')[1]
            if label=='before_stop':
                previous=r['buckets'];q=previous[3]['queue']
                if [actor_paths.get(p) for p in q[:3]]!=[52,50,48]:
                    raise ValueError('mutated actors were not the queued heads')
            elif label in LABELS[1:6]:
                changed={'after_stop':52,'after_owner':50,'after_remove':48}.get(label)
                for before,after in zip(previous,r['buckets']):
                    expected=[p for p in before['queue'] if changed is None or p!=actors[changed]['path']]
                    if after['queue']!=expected or any(after[k]!=before[k] for k in ('work','countdown','counter','clock')):
                        raise ValueError('mutation altered survivors, work or owner')
                if changed is not None:
                    if len(previous[3]['queue'])-len(r['buckets'][3]['queue'])!=1:
                        raise ValueError('public mutation did not retire one request')
                    mutations.append([label,changed,len(r['buckets'][3]['queue'])])
                previous=r['buckets']
                if label=='after_remove':next_head=previous[3]['head']
            elif label in ('next_tick','final_stopped') and any(s['queue'] for s in r['buckets']):
                raise ValueError('pending requests survived the next owner/final Stop')
            continue
        if event=='fine-result':continue
        if event=='search':
            if r['budget']!=(700 if r['kind']=='fine' else 400 if r['budget']<=400 else 5000):
                raise ValueError('path-owned search quota differs')
            if r['kind']=='fine':
                if not fine_results:raise ValueError('fine search lacks full result')
                result=fine_results.popleft()
                if result['work']!=r['pops'] or result['nodes']!=r['nodes'] or result['result']!=int(r['result']==1) or result['limit']!=r['budget']:
                    raise ValueError('fine result and actual search differ')
            searches.setdefault((r['path'],r['kind']),deque()).append(r['pops']);continue
        if event=='scheduler-class':
            if r['after']!=((r['before']&0xfdf0ffff)|((r['value']&0xff)<<16)):
                raise ValueError('class producer changed wrong bits')
            continue
        if event=='scheduler-target':
            expected=r['before']
            if bool(expected&0x04000000)!=bool(r['value']):expected=(expected&~0x06000000)|(0x04000000 if r['value'] else 0)
            if r['after']!=expected:raise ValueError('target producer changed wrong bits')
            continue
        if event in ('marker','scheduler-mutation-actor'):continue
        off=r['bucketOffset'];kind=(off%112)//28
        if not 0<=off<16*112 or off%28:raise ValueError('invalid policy address')
        before=r['stateBefore'] if event=='scheduler-admission' else r['before']
        after=r['stateAfter'] if event=='scheduler-admission' else r['after']
        state(before,kind);state(after,kind)
        if before['counter']!=after['counter'] or before['clock']!=after['clock']:
            raise ValueError('owner changed inside scheduler operation')
        if event=='scheduler-admission':
            q=list(before['queue']);p=r['path']
            accepted=before['work']<=LIMIT[kind] and (not q or q[0]==p)
            if r['result']!=int(accepted):raise ValueError('work/FIFO admission differs')
            admissions[p,kind]=r['result']
            if accepted and p in q:q.remove(p)
            elif not accepted and p not in q:q.append(p)
            if after['queue']!=q or after['work']!=before['work']:raise ValueError('admission changed wrong state')
            for raw,s in ((r['before'],before),(r['after'],after)):
                if raw!=[s['limit'],s['work'],s['countdown'],len(s['queue'])]:
                    raise ValueError('raw head/tail count snapshot differs')
            if next_head is not None and off==84:
                if p!=next_head or not accepted:raise ValueError('next surviving head did not gain admission')
                next_head=None
        elif event=='scheduler-unlink':
            if after['queue']!=[p for p in before['queue'] if p!=r['path']] or after['work']!=before['work'] or after['links']!=[0,0]:
                raise ValueError('unlink changed survivors/work or retained links')
        elif event=='scheduler-update':
            expected=(before['work'],before['countdown']-1) if before['countdown'] else (0,RELOAD[kind])
            if (after['work'],after['countdown'])!=expected or after['queue']!=before['queue']:
                raise ValueError('reset cadence differs')
        else:
            charge=0;admission=admissions.pop((r['path'],kind),None)
            if r['result']:
                if admission!=1:raise ValueError('request executed without admission')
                pending=searches.get((r['path'],'fine' if kind==3 else 'acc'))
                if pending:charge=pending.popleft()
                else:raise ValueError('request lacks actual search')
            if after['work']!=before['work']+charge:raise ValueError('search charge differs')
            slot=0 if kind==3 else 1;times=list(before['times'])
            if r['result']:times[slot]=0 if (after['work']<64 if kind==3 else charge<32) else before['counter']
            elif admission is not None:
                if admission!=0:raise ValueError('admitted request skipped its search')
                times[slot]=0
            elif before['counter']-before['times'][slot]>=10:raise ValueError('eligible request skipped admission')
            if after['times']!=times:raise ValueError('request timestamp differs')
    if next_head is not None or any(searches.values()) or fine_results:
        raise ValueError('unconsumed admission or search evidence')
    path=actors[50]['path']
    if not any(r.get('event')=='scheduler-class' and r['path']==path and r['value']==1 for r in rows):
        raise ValueError('owner reclassification missing')
    removed=next(i for i,r in enumerate(rows) if r.get('event')=='scheduler-mutation-marker' and 'label=after_remove' in r['value'])
    requeued=[]
    for actor,off in ((52,84),(50,196)):
        later=[r for r in rows[removed+1:] if r.get('event')=='scheduler-admission' and r['path']==actors[actor]['path']]
        if not later or any(r['bucketOffset']//112!=off//112 for r in later) or not any(r['bucketOffset']==off and r['result']==1 for r in later):
            raise ValueError('replacement failed to admit in its current owner row')
        requeued.append([actor,off,len(later)])
    if any(r.get('event')=='scheduler-admission' and r['path']==actors[48]['path'] for r in rows[removed+1:]):
        raise ValueError('removed path was admitted again')
    return dict(events=len(stream),mutations=mutations,requeued=requeued,counts={e:counts[e] for e in EVENTS},
                digest=hashlib.sha256(json.dumps(stream,separators=(',',':')).encode()).hexdigest(),stream=stream)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--repeat',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    read=lambda p:[json.loads(l) for l in p.read_text().splitlines()]
    result=verify(read(args.capture));repeat=verify(read(args.repeat))
    if result.pop('stream')!=repeat['stream']:raise ValueError('ordered mutation repeats differ')
    result.update(repeat_verified=True,capture_sha256=hashlib.sha256(args.capture.read_bytes()).hexdigest(),
                  repeat_sha256=hashlib.sha256(args.repeat.read_bytes()).hexdigest(),source_sha256=SOURCE_HASHES)
    args.output.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
