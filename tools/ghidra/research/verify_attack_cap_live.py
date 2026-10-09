#!/usr/bin/env python3
"""Check real zero-damage notifications, exact rearm words and repeat/control.

This checks the Attack timer producer, not complete engine trajectories or
unobserved notification guards. Hooks are read-only; public positions are
three-decimal script samples. Original-code fixtures own adjacent boundaries.
"""
import argparse, gzip, hashlib, json, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_wc3_pathing_numeric import add, subtract, bits

SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
EVENTS={'marker','notice','begin','expire','exempt'}

def normalized(value):
    if isinstance(value,dict):
        return {k:normalized(v) for k,v in value.items() if k not in ('seq','ability','request')}
    if isinstance(value,list):return [normalized(v) for v in value]
    return value

def observed(path):
    rows=[json.loads(line) for line in path.read_text().splitlines()]
    meta=[r for r in rows if r['event']=='metadata']
    end=[r for r in rows if r['event']=='trace-end']
    pre=[r for r in rows if r['event']=='preload-file']
    if len(meta)!=1 or meta[0]['sha256']!=SHA or meta[0]['mode']!='observe':raise ValueError('observed provenance')
    if len(end)!=1 or not end[0]['installed'] or len(pre)!=1 or not pre[0]['complete']:raise ValueError('incomplete observation')
    if any(r['event'] in ('error','trace-failed') for r in rows):raise ValueError('observer failure')
    for event in EVENTS:
        if sum(r['event']==event for r in rows)!=end[0]['counts'].get(event,0):raise ValueError('truncated stream')
    markers=[r['value'] for r in rows if r['event']=='marker']
    if len(markers)!=89 or 'tick=80 label=complete' not in markers[-1]:raise ValueError('completion markers')
    return rows,meta[0],markers

def verify(first,repeat,control,preload):
    a,ma,markers=observed(first);b,mb,other=observed(repeat)
    for key in ('sha256','source_sha256','map','prefix'):
        if ma[key]!=mb[key]:raise ValueError('repeat inputs')
    stream=[normalized(r) for r in a if r['event'] in EVENTS]
    if stream!=[normalized(r) for r in b if r['event'] in EVENTS]:raise ValueError('semantic repeat')
    rows=[json.loads(line) for line in control.read_text().splitlines()]
    meta=[r for r in rows if r['event']=='metadata'];pre=[r for r in rows if r['event']=='preload-file']
    if len(meta)!=1 or meta[0]['mode']!='control' or meta[0]['sha256']!=SHA:raise ValueError('control provenance')
    for key in ('source_sha256','map','prefix'):
        if ma[key]!=meta[0][key]:raise ValueError('control inputs')
    if any(r['event'] in EVENTS|{'module','trace-end','trace-failed','error'} for r in rows):raise ValueError('control hooks')
    if len(pre)!=1 or not pre[0]['complete']:raise ValueError('incomplete control')
    raw=preload.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=pre[0]['sha256']:raise ValueError('control file')
    if re.findall(r'call Preload\( "(AC151 [^"\r\n]*)" \)',raw.decode())!=markers:raise ValueError('control timeline')
    value=lambda word:struct.unpack('<f',struct.pack('<I',word))[0]
    armed=held=0;begins=[r for r in a if r['event']=='begin']
    notices=[r for r in a if r['event']=='notice'];expires=[r for r in a if r['event']=='expire']
    if len(begins)!=8 or len(notices)!=8 or len(expires)!=1:raise ValueError('producer coverage')
    for n,r in zip(notices,begins):
        if n['damage']!=0 or n['flags']!=0x100 or n['tick']!=r['tick'] or r['caller']!='493663':raise ValueError('zero damage producer')
        before,after=r['before'],r['after']
        rearm=not before['active'] or value(subtract(bits(3),subtract(before['deadline'],before['clock'])))>=.5
        if not after['active']:raise ValueError('missing active timer')
        if rearm:
            if after['deadline']!=add(after['clock'],bits(3)):raise ValueError('arm deadline')
            if before['active'] and after['serial']==before['serial']:raise ValueError('missing rearm serial')
            armed+=1
        else:
            if any(before[k]!=after[k] for k in ('deadline','serial')):raise ValueError('held deadline changed')
            held+=1
    if (armed,held)!=(3,5):raise ValueError('throttle coverage')
    # The clock clears its request before dispatching d01bd; the hook sees
    # inactive control, not the popped deadline. Exact expiry ordering is
    # covered by the original clock fixture and production timer regression.
    if expires[0]['before']['active']:raise ValueError('expiry control still active')
    exempt=[r for r in a if r['event']=='exempt']
    if [r['value'] for r in exempt]!=[1,0] or any(bool(r['flags']&0x1000000)!=bool(r['value']) for r in exempt):raise ValueError('exemption lifetime')
    return dict(binary_sha256=SHA,passed=True,notifications=8,arms=armed,held=held,expiries=1,
                markers=len(markers),repeats=2,scope=__doc__,
                semantic_sha256=hashlib.sha256(json.dumps(stream,sort_keys=True).encode()).hexdigest(),
                input_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (first,repeat,control,preload)})

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('first','repeat','control','preload','report'):ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--expected',type=Path)
    args=ap.parse_args();result=verify(args.first,args.repeat,args.control,args.preload)
    if args.expected and result!=json.loads(gzip.decompress(args.expected.read_bytes())):raise ValueError('frozen witness differs')
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
