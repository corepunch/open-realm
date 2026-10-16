#!/usr/bin/env python3
"""Exact public modal-order records, original admission and committed movement."""
import argparse
import hashlib
import json
from pathlib import Path


def normalize(rows):
    def word(x):
        if type(x)is not int or not 0<=x<=0xffffffff:raise ValueError('metadata requires uint32 words')
        return x
    def words(x,n):
        if type(x)is not list or len(x)!=n:raise ValueError('metadata word array shape differs')
        return [word(v)for v in x]
    units={};abilities={};orders={};out=[]
    def identity(table,key):
        if key not in table:table[key]=len(table)
        return table[key]
    def queue(q):
        def order(pair):
            words(pair,2)
            return None if pair==[0xffffffff,0xffffffff]else identity(orders,tuple(pair))
        return dict(head=order(q['head']),tail=order(q['tail']),count=word(q['count']))
    for r in rows:
        e=r.get('event')
        if e=='metadata-marker':out.append(dict(event=e,value=r['value']))
        elif e=='metadata-row':
            if r['kind']not in ('real','integer'):raise ValueError('unknown exported metadata type')
            out.append(dict(event=e,kind=r['kind'],parent=word(r['parent']),child=word(r['child']),word=word(r['word'])))
        elif e in ('metadata-interception','metadata-clear-pending'):
            x=dict(event=e,unit=identity(units,r['unit']),before=queue(r['before']),after=queue(r['after']),caller=word(r['caller']))
            if e=='metadata-interception':x.update(command=word(r['command']),result=word(r['result']))
            # CancelQueuedOrderSuccessors returns void. Its incidental EAX is not output.
            out.append(x)
        elif e in ('metadata-toggle-validation','metadata-defend-event'):
            out.append(dict(event=e,ability=identity(abilities,r['ability']),**{k:word(r[k])for k in ('vtable','flags','input','caller','result','afterFlags')}))
        elif e=='velocity-commit':
            out.append(dict(event=e,speed=word(r['speed']),heading=word(r['heading']),before=words(r['before'],8),after=words(r['after'],8),clock=words(r['clock'],3)))
    return out


def verify(raw,fixture,cap):
    if hashlib.sha256(raw).hexdigest()!=cap['sha256']or len(raw)!=cap['bytes']:raise ValueError('metadata capture hash/length differs')
    rows=[json.loads(s)for s in raw.splitlines()if s.strip()]
    meta=[r for r in rows if r.get('event')=='metadata'];ends=[r for r in rows if r.get('event')=='trace-end']
    if len(meta)!=1 or len(ends)!=1 or not ends[0].get('installed')or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows):raise ValueError('incomplete/error metadata capture')
    if {k:v for k,v in meta[0].items()if k not in ('event','pid')}!=cap['metadata']:raise ValueError('metadata producer provenance differs')
    if sum(r.get('event')=='metadata-marker'and r.get('value')=='PATHMETA complete'for r in rows)!=1:raise ValueError('metadata producer completion missing')
    actual=normalize(rows)
    if actual!=fixture['events']:raise ValueError('modal words/admission/movement differ')
    exported=[r for r in actual if r['event']=='metadata-row'];motion=[r for r in actual if r['event']=='velocity-commit']
    if len(exported)!=fixture['records']*6 or len(motion)!=fixture['motion_commits']:raise ValueError('modal record/motion count differs')
    count=ends[0].get('counts',{}).get('velocity-commit')
    if type(count)is not int or count!=len(motion):raise ValueError('modal footer motion count differs')
    if [(r['parent'],r['child'])for r in exported]!=[(i,j)for i in range(fixture['records'])for j in range(6)]:raise ValueError('modal exported rows incomplete')
    return dict(records=fixture['records'],motion_commits=len(motion),interceptions=sum(r['event']=='metadata-interception'for r in actual),validations=sum(r['event']=='metadata-toggle-validation'for r in actual))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',type=Path,nargs=2,required=True);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    f=json.loads(a.fixture.read_text())
    if f.get('version')!=1 or len(f['captures'])!=2:p.error('requires version1 two-repeat fixture')
    counts=[verify(path.read_bytes(),f,cap)for path,cap in zip(a.capture,f['captures'])]
    report=dict(passed=True,captures=2,**{k:sum(c[k]for c in counts)for k in counts[0]},scope=f['scope'])
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
