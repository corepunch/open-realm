#!/usr/bin/env python3
"""Verify complete retail ally-permission or help-radius repeat/control captures.

Ignore only address allocation, recorder sequence, wall-time controls and unit-map
lookup counts outside the help chain. Preserve all help/candidate/gate/notification,
cap deadline and public marker data. Pose mode also preserves canonical fine words.
This verifies retail evidence, not complete engine trajectories or AI enrollment.
"""
import argparse,gzip,hashlib,json,re
from pathlib import Path

SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'

def read(path):
    rows=[json.loads(line) for line in path.read_text().splitlines()]
    if any(r.get('event') in ('error','observer-error','script-error','trace-failed') for r in rows):
        raise ValueError('capture error')
    if not rows or rows[0].get('event')!='metadata' or rows[0].get('sha256')!=SHA or not rows[0].get('owned'):
        raise ValueError('capture provenance')
    ends=[r for r in rows if r.get('event')=='preload-file']
    if len(ends)!=1 or not ends[0].get('complete'):raise ValueError('incomplete public operation')
    return rows

def normalize(rows):
    addresses={}
    def value(v,key=''):
        if isinstance(v,dict):return {k:value(x,k) for k,x in v.items() if k!='seq'}
        if isinstance(v,list):return [value(x) for x in v]
        if key in ('address','ability','request','argument') and isinstance(v,str) and v.startswith('0x') and int(v,16)>65535:
            if v not in addresses:addresses[v]=len(addresses)
            return addresses[v]
        return v
    return [value(r) for r in rows if r['event'] not in
            ('metadata','module','loading-key','trace-end','preload-file','unit-map')]

def verify(first,repeat,control,preload):
    a,b,c=(read(p) for p in (first,repeat,control))
    if [r[0]['mode'] for r in (a,b,c)]!=['observe','observe','control']:raise ValueError('observer control modes')
    if b[0]['source_sha256']!=a[0]['source_sha256']:raise ValueError('repeat input hashes')
    # The control injects no observer. Its recorded unused observer can differ;
    # map and capture/map producers must still match the observed experiment.
    inputs=lambda metadata:{k:v for k,v in metadata['source_sha256'].items() if not k.endswith('.js')}
    if inputs(c[0])!=inputs(a[0]):raise ValueError('control input hashes')
    for capture in (a,b):
        ends=[r for r in capture if r['event']=='trace-end']
        if len(ends)!=1 or not ends[0].get('installed'):raise ValueError('observer incomplete')
        counts={}
        for row in capture:
            if 'seq' in row:counts[row['event']]=counts.get(row['event'],0)+1
        if ends[0]['counts']!=counts:raise ValueError('truncated observer stream')
    if any('seq' in row for row in c):raise ValueError('control contains hooks')
    raw=preload.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=c[-1].get('sha256'):raise ValueError('control file hash')
    prefix=a[0]['prefix']
    markers=re.findall(r'Preload\( "('+re.escape(prefix)+r'[^"\r\n]*)" \)',raw.decode())
    observed=[[row['value'] for row in capture if row['event']=='marker'] for capture in (a,b)]
    if not markers or observed!=[markers,markers] or len(markers)!=c[-1]['markers']:raise ValueError('public control markers')
    if 'label=complete' not in markers[-1]:raise ValueError('missing final marker')
    stream=normalize(a)
    if stream!=normalize(b):raise ValueError('retail semantic repeat differs')
    mode='help' if prefix=='AH152 ' else 'permissions' if prefix=='AA152 ' else None
    if mode is None:raise ValueError('unknown experiment')
    counts={kind:sum(r['event']==kind for r in a) for kind in ('help','help-radius','help-arm','candidate','ally')}
    if mode=='permissions':
        if counts!={'help':4,'help-radius':0,'help-arm':0,'candidate':5,'ally':1} or len(markers)!=209:
            raise ValueError('directional admission coverage')
        ally=next(r for r in a if r['event']=='ally')
        if ally['tick']!=160 or ally['helper']['owner']!=1 or not ally['after']['active']:
            raise ValueError('wrong directional admission')
    else:
        expected=[(10,0,1127022592),(41,0,1127022592),(60,12,1130692608),
                  (89,12,1130692608),(110,0,1127022592),(141,0,1127022592)]
        radii=[(r['tick'],r['victim']['owner'],r['radius']) for r in a if r['event']=='help-radius']
        arms=[(r['tick'],r['victim']['owner'],r['delay']) for r in a if r['event']=='help-arm']
        if radii!=expected or arms!=[(tick,owner,1056964608 if owner==12 else 1077936128) for tick,owner,_ in expected]:
            raise ValueError('radius/cooldown contract')
        if counts!={'help':15,'help-radius':6,'help-arm':6,'candidate':30,'ally':31} or len(markers)!=190:
            raise ValueError('help admission coverage')
        if any(r['normalDelay']!=1077936128 or r['aiRadius']!=1147207680 for r in a if r['event']=='help-radius'):
            raise ValueError('native runtime constants')
        for tick,owner,_ in expected:
            candidates=[r for r in a if r['event']=='candidate' and r['tick']==tick]
            if [r['helper']['owner'] for r in candidates]!=[1,1,1,1,owner]:raise ValueError('recipient ordering')
            if any('fine' in r['helper'] and r['helper']['fine'] is None for r in candidates):raise ValueError('unresolved canonical pose')
    return dict(binary_sha256=SHA,passed=True,mode=mode,markers=len(markers),repeats=2,counts=counts,events=stream)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('first','repeat','control','preload','report'):ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--expected',type=Path);ap.add_argument('--fixture',type=Path)
    args=ap.parse_args();report=verify(args.first,args.repeat,args.control,args.preload)
    if args.expected and report!=json.loads(gzip.decompress(args.expected.read_bytes())):raise ValueError('frozen witness differs')
    if args.fixture:args.fixture.write_bytes(gzip.compress((json.dumps(report,separators=(',',':'))+'\n').encode(),mtime=0))
    args.report.write_text(json.dumps({k:v for k,v in report.items() if k!='events'},indent=2)+'\n')
    print(args.report.read_text())
if __name__=='__main__':main()
