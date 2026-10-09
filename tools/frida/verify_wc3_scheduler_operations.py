"""Validate the shared retail scheduler operations without scenario-specific claims."""
from collections import deque
import hashlib
import json

from verify_wc3_scheduler_trace import LIMIT, RELOAD

OPERATIONS=('scheduler-class','scheduler-target','scheduler-admission','scheduler-unlink',
            'scheduler-update','scheduler-acc-request','scheduler-fine-request')


def verify_operations(rows,events):
    names={};stream=[];searches={};fine_results=deque(r for r in rows if r.get('event')=='fine-result');admissions={};
    def normalize(x,key=None):
        if isinstance(x,dict):return {k:normalize(v,k) for k,v in x.items() if k not in ('ms','system')}
        if isinstance(x,list):
            if key=='links':return [normalize(hex(v&0xffffffff)) for v in x]
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
        if event not in events:continue
        stream.append(normalize(r))
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
        if event not in OPERATIONS:continue
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
    if any(searches.values()) or fine_results:raise ValueError('unconsumed search evidence')
    return dict(events=len(stream),stream=stream,
                digest=hashlib.sha256(json.dumps(stream,separators=(',',':')).encode()).hexdigest())
