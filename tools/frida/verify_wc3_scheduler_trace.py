"""Check retail scheduler state transitions and normalize the ordered budget stream."""
import argparse
from collections import Counter, deque
import hashlib
import json
import re
from pathlib import Path

HASH='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCE_HASHES={
    'wc3_scheduler_probe.j':'f46322880faaa000ac98766406f17b4ba31512a47bd2efb0942cb1eebbbbbb2f',
    'trace_wc3_pathfinding.py':'36287c1140683f046444943124b0c4e75077f27a50a726af5eb5d1bd486a0338',
    'wc3_pathfinding.js':'010170f6035f990447147e00d7020b4366ce3b1849444e7ad5251d0fd06b1545',
    'make_wc3_pathfinding_map.py':'6a6f9a5cd01c06a3e2031a91e1543f84437d38d999cb90d6e65518bcd9cf6a95',
}
RELOAD=(3,2,2,1)
LIMIT=(800,300,900,1100)
EVENTS=('scheduler-admission','scheduler-update','scheduler-unlink',
        'scheduler-acc-request','scheduler-fine-request')


def verify(rows):
    metadata=[r for r in rows if r.get('event')=='metadata']
    footer=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or metadata[0].get('sha256')!=HASH or not metadata[0].get('schedulerEvents'):
        raise ValueError('missing pinned scheduler metadata')
    if any(metadata[0].get('source_sha256',{}).get(name)!=digest for name,digest in SOURCE_HASHES.items()):
        raise ValueError('scheduler producer/observer generation differs')
    if len(footer)!=1 or not footer[0].get('installed') or rows[-1]!=footer[0]:
        raise ValueError('capture is incomplete')
    if any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):
        raise ValueError('observer failed')
    counts=Counter(r.get('event') for r in rows)
    for event in EVENTS+('acc-search','fine-search'):
        recorded=counts[event]
        if event in ('acc-search','fine-search'):
            recorded=sum(r.get('event')=='search' and r.get('kind')==event.split('-')[0] for r in rows)
        if recorded!=footer[0]['counts'].get(event,0):
            raise ValueError('scheduler stream truncated: '+event)
    markers=[r.get('value','') for r in rows if r.get('event')=='marker']
    samples=[int(re.search(r'tick=(\d+)',m)[1]) for m in markers if ' label=sample ' in m]
    if samples!=list(range(1,301)):
        raise ValueError('sample timeline is incomplete or duplicated')
    waves=[(int(re.search(r'tick=(\d+)',m)[1]),m.split('label=')[1].split()[0]) for m in markers if ' label=wave_' in m]
    if waves!=[(t,label) for t in (10,100,190) for label in ('wave_before','wave_after')]:
        raise ValueError('three producer waves differ')
    if sum('tick=300 label=complete ' in m for m in markers)!=1:
        raise ValueError('missing complete 30-second producer timeline')
    names={};searches={};stream=[];denials=Counter();resets=Counter();first=[];window=[]
    pending={};waits={};retired=Counter();admissions={};complete=False;last_counter=0
    def identity(path):
        if path in ('0x0','0xffffffff'):return path
        if path not in names:names[path]=len(names)
        return names[path]
    def state(s):
        queue=s['queue']
        if len(set(queue))!=len(queue) or s['head']!=(queue[0] if queue else '0x0') or s['tail']!=(queue[-1] if queue else '0x0'):
            raise ValueError('corrupt FIFO snapshot')
        return [s['counter'],s['work'],s['countdown'],[identity(p) for p in queue]]
    for index,r in enumerate(rows):
        event=r.get('event')
        if event=='marker' and 'tick=300 label=complete ' in r.get('value',''):
            complete=True;pending_at_complete=len(pending)
            pending_boundary=[dict(path=identity(p),player=owner,policy=kind,observed_owner_passes=last_counter-start)
                              for (p,owner,kind),start in pending.items()]
        if event=='search':
            searches.setdefault((r['path'],r['kind']),deque()).append(r['pops'])
        if event not in EVENTS:continue
        off=r['bucketOffset'];kind=(off%112)//28;player=off//112
        if not 0<=player<16 or off%28:raise ValueError('invalid policy address')
        before=r['stateBefore'] if event=='scheduler-admission' else r['before']
        after=r['stateAfter'] if event=='scheduler-admission' else r['after']
        if before['limit']!=LIMIT[kind] or after['limit']!=LIMIT[kind]:raise ValueError('policy work limit differs')
        a=state(before);b=state(after);path=identity(r['path']) if 'path' in r else None
        last_counter=after['counter']
        if before['counter']!=after['counter']:raise ValueError('owner changed inside scheduler operation')
        if event=='scheduler-admission':
            queue=list(before['queue']);p=r['path']
            admitted=before['work']<=before['limit'] and (not queue or queue[0]==p)
            if r['result']!=int(admitted):raise ValueError('work/FIFO admission differs')
            admissions[r['path'],kind]=r['result']
            if admitted and p in queue:queue.remove(p)
            elif not admitted and p not in queue:queue.append(p)
            if after['queue']!=queue or after['work']!=before['work']:raise ValueError('admission changed work or queue incorrectly')
            if not admitted and not complete:denials[player,kind]+=1
            key=(r['path'],player,kind)
            if not admitted and p not in before['queue']:
                pending[key]=before['counter']
            if admitted and key in pending:
                duration=before['counter']-pending.pop(key)
                if not complete:waits.setdefault((player,kind),[]).append(duration)
        elif event=='scheduler-unlink':
            following=rows[index+1] if index+1<len(rows) else {}
            nested=following.get('event')=='scheduler-admission' and following.get('path')==r['path'] and following.get('result')==1
            key=(r['path'],player,kind)
            if not nested and r['path'] in before['queue'] and key in pending:
                pending.pop(key)
                if not complete:retired[player,kind]+=1
            queue=[p for p in before['queue'] if p!=r['path']]
            if after['queue']!=queue or after['work']!=before['work']:raise ValueError('unlink changed work or survivors')
        elif event=='scheduler-update':
            work=before['work'] if before['countdown'] else 0
            countdown=before['countdown']-1 if before['countdown'] else RELOAD[kind]
            if (after['work'],after['countdown'],after['queue'])!=(work,countdown,before['queue']):
                raise ValueError('reset cadence differs')
            if not before['countdown'] and not complete:resets[player,kind]+=1
        else:
            charge=0
            if r['result']:
                searches_for_path=searches.get((r['path'],'fine' if kind==3 else 'acc'))
                if searches_for_path:
                    charge=searches_for_path.popleft()
                else:
                    route=rows[index+1] if index+1<len(rows) else {}
                    if not (kind!=3 and after['work']==before['work'] and
                            route.get('event')=='route' and route.get('kind')=='acc' and
                            route.get('path')==r['path'] and route.get('result')==1 and
                            route.get('count')==1 and len(route.get('points',[]))==1 and
                            not route.get('truncated')):
                        raise ValueError('accepted request lacks actual search work or verified setup bypass')
            if after['work']!=before['work']+charge:raise ValueError('search charge differs')
            admission=admissions.pop((r['path'],kind),None)
            slot=0 if kind==3 else 1
            expected=list(before['times'])
            if r['result']:
                if admission!=1:raise ValueError('request ran without admission')
                expected[slot]=0 if (after['work']<64 if kind==3 else charge<32) else before['counter']
            elif admission is not None:
                if admission!=0:raise ValueError('admitted request did not execute')
                expected[slot]=0
            elif before['counter']-before['times'][slot]>=10:
                raise ValueError('request skipped eligible admission')
            if after['times']!=expected:raise ValueError('request timestamp differs')
            transaction=[before['counter'],kind,path,player,r['result'],charge,*before['times'],*after['times'],
                         before['work'],after['work'],len(before['queue']),len(after['queue']),
                         identity(after['head']),identity(after['tail']),before['countdown'],after['countdown']]
            if before['counter']==1058:first.append(transaction)
            if 1058<=before['counter']<=1062:window.append(transaction)
        if not complete:stream.append([event,off,path,r.get('result'),a,b,before.get('times'),after.get('times')])
    if any(searches.values()):raise ValueError('search work was not consumed by its request')
    for player in range(2):
        for kind in (0,3):
            if not denials[player,kind] or resets[player,kind]<2:
                raise ValueError('fixture lacks repeated two-player budget exhaustion')
    return dict(events=len(stream),paths=len(names),denials={f'{p}:{k}':v for (p,k),v in sorted(denials.items())},
                resets={f'{p}:{k}':v for (p,k),v in sorted(resets.items())},first_owner=first,first_window=window,
                waits={f'{p}:{k}':dict(admitted=len(v),maximum_owner_passes=max(v)) for (p,k),v in sorted(waits.items())},
                retired={f'{p}:{k}':v for (p,k),v in sorted(retired.items())},pending=pending_at_complete,pending_boundary=pending_boundary,
                stream=stream,digest=hashlib.sha256(json.dumps(stream,separators=(',',':')).encode()).hexdigest())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--repeat',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--header',type=Path)
    args=parser.parse_args()
    result=verify([json.loads(x) for x in args.capture.read_text().splitlines()])
    if args.repeat:
        repeat=verify([json.loads(x) for x in args.repeat.read_text().splitlines()])
        if result['stream']!=repeat['stream']:raise ValueError('ordered scheduler repeats differ')
        result['repeat_verified']=True
    result['capture_sha256']=hashlib.sha256(args.capture.read_bytes()).hexdigest()
    if args.repeat:result['repeat_sha256']=hashlib.sha256(args.repeat.read_bytes()).hexdigest()
    if args.header:
        text='/* Generated by verify_wc3_scheduler_trace.py from pinned retail Scheduler102. */\n'
        text+='/* '+result['capture_sha256']+' */\n'
        text+='static uint32_t const retail_scheduler_contention[][18]={\n'
        for row in result['first_window']:
            text+='    {'+','.join('UINT32_MAX' if isinstance(v,str) else str(v) for v in row)+'},\n'
        args.header.write_text(text+'};\n')
    result.pop('stream')
    args.output.write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__':main()
