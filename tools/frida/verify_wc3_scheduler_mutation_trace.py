#!/usr/bin/env python3
"""Verify read-only retail queue mutation and the next ordered admissions."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

from verify_wc3_scheduler_trace import HASH, LIMIT
from verify_wc3_scheduler_operations import verify_operations

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
    operations=verify_operations(rows,EVENTS);next_head=None;mutations=[]
    def state(s,kind):
        q=s['queue']
        if s['limit']!=LIMIT[kind] or len(q)!=len(set(q)) or s['head']!=(q[0] if q else '0x0') or s['tail']!=(q[-1] if q else '0x0'):
            raise ValueError('invalid FIFO or work limit')
    for r in rows:
        event=r.get('event')
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
        if event=='scheduler-admission' and next_head is not None and r['bucketOffset']==84:
            if r['path']!=next_head or not r['result']:raise ValueError('next surviving head did not gain admission')
            next_head=None
    if next_head is not None:raise ValueError('next surviving head never attempted admission')
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
    return dict(operations,mutations=mutations,requeued=requeued,counts={e:counts[e] for e in EVENTS})



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
