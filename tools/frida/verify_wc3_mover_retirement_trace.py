#!/usr/bin/env python3
"""Verify complete public mover death/removal and retained-versus-released owners."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

from verify_wc3_scheduler_trace import HASH
from verify_wc3_scheduler_operations import verify_operations, OPERATIONS

SOURCE_HASHES={
    'wc3_mover_retirement_probe.j':'a24fd1ecf0049b40bb70c1bbf0c17b5fb09fa43834a08b8f875604bb700fc579',
    'trace_wc3_pathfinding.py':'400e5189980c6a5d5589797f112eb9441bb149219ba478a78a2c4014252a84b9',
    'wc3_pathfinding.js':'1e0fed527c071a0ba28d749986ca96f1947294bbfbdb6a5d4ea793601872decd',
    'make_wc3_pathfinding_map.py':'6c5e5999f043cc843603fa090163ab4f49903c6a774293285a895eca71534fda',
    'map':'efdc5c1cecd09ee22c982ae21928e17ad033e894c4570e37f40c42c619a4443d',
}
EVENTS=OPERATIONS+('search','fine-result','marker','scheduler-mutation-actor',
    'scheduler-mutation-marker','mover-retirement-marker','point-task','task-prepend',
    'task-cleanup','task-recovery','task-arrival','task-cant-path','mover-stop')
BOUNDARIES=[('pending_before',52),('pending_killed',52),('pending_remove_before',50),
    ('pending_removed',50),('pending_removed_next',50),('travel_before',94),
    ('travel_killed',94),('travel_remove_before',95),('travel_removed',95),
    ('travel_removed_next',95),('dead_retained',52),('dead_travel_retained',94),
    ('dead_released',52),('dead_travel_released',94)]


def verify(rows):
    metadata=[r for r in rows if r.get('event')=='metadata']
    footer=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or metadata[0].get('sha256')!=HASH or not all(metadata[0].get(k) for k in ('schedulerEvents','fineResultEvents','taskEvents')):
        raise ValueError('missing pinned retirement metadata')
    if any(metadata[0].get('source_sha256',{}).get(n)!=h for n,h in SOURCE_HASHES.items()):
        raise ValueError('retirement producer/observer generation differs')
    if len(footer)!=1 or rows[-1]!=footer[0] or not footer[0].get('installed'):
        raise ValueError('capture is incomplete')
    if any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):raise ValueError('observer failed')
    counts=Counter(r.get('event') for r in rows)
    for event in EVENTS:
        if event not in ('search','fine-result','marker') and counts[event]!=footer[0]['counts'].get(event,0):
            raise ValueError('retirement stream truncated: '+event)
    for kind in ('fine','acc'):
        if sum(r.get('event')=='search' and r['kind']==kind for r in rows)!=footer[0]['counts'].get(kind+'-search',0):
            raise ValueError('search stream truncated')
    if counts['fine-result']!=footer[0]['counts'].get('fine-search',0):raise ValueError('fine results missing')
    samples=[int(re.search(r'tick=(\d+)',r['value'])[1]) for r in rows if r.get('event')=='marker' and 'label=sample ' in r['value']]
    phases=[r['value'].split('label=')[1].split()[0] for r in rows if r.get('event')=='marker' and 'label=sample ' not in r['value']]
    if samples!=list(range(1,46)) or phases!=['start_mover_retirement','wave_before','wave_after','complete']:
        raise ValueError('complete producer timeline missing')
    actors=[r for r in rows if r.get('event')=='scheduler-mutation-actor'][:96]
    if len(actors)!=96 or len({r['path'] for r in actors})!=96 or any(r['mode']!=1 for r in actors):
        raise ValueError('initial public actor bindings missing')
    snapshots=[r for r in rows if r.get('event')=='mover-retirement-marker']
    if [(r['value'].split('label=')[1].split()[0],r['actor']) for r in snapshots]!=BOUNDARIES:
        raise ValueError('retirement boundaries missing')
    by_label={r['value'].split('label=')[1].split()[0]:r for r in snapshots}
    for before,after,travel,kill in [('pending_before','pending_killed',False,True),
        ('pending_remove_before','pending_removed',False,False),('travel_before','travel_killed',True,True),
        ('travel_remove_before','travel_removed',True,False)]:
        a,b=by_label[before],by_label[after]
        if not all(a[k] and b[k] for k in ('unitLive','moverLive','pathLive','originalGroupLive','originalGroupPathLive')):
            raise ValueError('public return released owned storage prematurely')
        if a['groupMembers']!=1 or a['originalGroupMembers']!=1 or b['groupMembers']!=0 or b['originalGroupMembers']!=1:
            raise ValueError('detached member row did not survive until owner pruning')
        if a['orderCount']!=1 or a['orderHead']==[-1,-1] or b['orderCount']!=0 or b['orderHead']!=[-1,-1]:
            raise ValueError('user order did not retire synchronously')
        if b['taskHead']==[-1,-1]:raise ValueError('replacement death/removal task is missing')
        if a['unitFlags']!=512 or b['unitFlags']!=(800 if kill else 520):raise ValueError('death/removal unit policy differs')
        if travel and (a['velocity']==[0,0] or not a['routeCounts'][0]):raise ValueError('travel witness was not moving')
        if not travel and (a['velocity']!=[0,0] or a['links']==[0,0]):raise ValueError('pending witness was not queued')
        if b['velocity']!=[0,0] or b['links']!=[0,0] or b['groupIdentity']!=[-1,-1] or b['group']!='0x0':
            raise ValueError('retired task retained displacement/group/FIFO ownership')
        if b['routeCounts']!=[0,0] or b['routeIndices']!=[-1,-1] or b['destination']!=[-939917312,-939917312] or not b['pathFlags']&0x100000:
            raise ValueError('retired path kept active route state')
        if b['pose']!=a['pose']:raise ValueError('retirement moved the sampled pose')
    for label in ('pending_removed_next','travel_removed_next','dead_released','dead_travel_released'):
        r=by_label[label]
        if any(r[k] for k in ('unitLive','moverLive','pathLive','originalGroupLive','originalGroupPathLive')):
            raise ValueError('deferred removal leaked an owner')
    for label in ('dead_retained','dead_travel_retained'):
        r=by_label[label]
        if not all(r[k] for k in ('unitLive','moverLive','pathLive')) or any(r[k] for k in ('originalGroupLive','originalGroupPathLive')):
            raise ValueError('corpse and detached group lifetimes were conflated')
        if r['orderCount'] or r['velocity']!=[0,0] or r['routeCounts']!=[0,0]:raise ValueError('corpse resumed its retired task')
    queues=[r for r in rows if r.get('event')=='scheduler-mutation-marker']
    if [r['value'].split('label=')[1] for r in queues]!=['pending_before','pending_killed','pending_removed','final_stopped']:
        raise ValueError('pending FIFO boundaries missing')
    for r in queues:
        if [b['offset'] for b in r['buckets']]!=list(range(0,224,28)):raise ValueError('missing player/policy snapshot')
        for s in r['buckets']:
            q=s['queue']
            if len(q)!=len(set(q)) or s['head']!=(q[0] if q else '0x0') or s['tail']!=(q[-1] if q else '0x0'):
                raise ValueError('corrupt retirement FIFO')
    for index,actor in ((0,52),(1,50)):
        a,b=queues[index]['buckets'],queues[index+1]['buckets'];path=actors[actor]['path']
        if a[3]['head']!=path:raise ValueError('retired pending actor was not the head')
        for old,new in zip(a,b):
            if new['queue']!=[p for p in old['queue'] if p!=path] or any(new[k]!=old[k] for k in ('work','countdown','counter','clock')):
                raise ValueError('retirement changed survivors or budget')
    if any(s['queue'] for s in queues[-1]['buckets']):raise ValueError('final Stop left queued requests')
    for label,actor in (('pending_killed',52),('pending_removed',50),('travel_killed',94),('travel_removed',95)):
        index=next(i for i,r in enumerate(rows) if r.get('event')=='mover-retirement-marker' and r['value'].startswith('PATHRETIRE label='+label+' '))
        if any(r.get('event')=='scheduler-admission' and r['path']==actors[actor]['path'] for r in rows[index+1:]):
            raise ValueError('retired path was admitted again')
    result=verify_operations(rows,EVENTS)
    result.update(boundaries=len(snapshots),counts={e:counts[e] for e in EVENTS})
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path);parser.add_argument('--repeat',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    read=lambda p:[json.loads(l)for l in p.read_text().splitlines()]
    result=verify(read(args.capture));repeat=verify(read(args.repeat))
    if result.pop('stream')!=repeat['stream']:raise ValueError('ordered retirement repeats differ')
    result.update(repeat_verified=True,capture_sha256=hashlib.sha256(args.capture.read_bytes()).hexdigest(),
        repeat_sha256=hashlib.sha256(args.repeat.read_bytes()).hexdigest(),source_sha256=SOURCE_HASHES)
    args.output.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
