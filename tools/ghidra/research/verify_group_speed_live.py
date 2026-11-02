#!/usr/bin/env python3
"""Verify delivered Follow speed words and full repeat/control streams.

Read-only Frida evidence. Committed positions lack elapsed prediction inputs,
so this verifier does not infer the strict distance gate or claim engine motion.
"""
import argparse,gzip,hashlib,json,re,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from verify_wc3_pathing_numeric import multiply
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
EVENTS={'commit','share','exempt','cohort','req-flag','marker','globals'}
DROP={'seq','mover','group','member','request','row','path','shared','chain','from','caller','ms'}

def normalize(x):
    if isinstance(x,dict):return {k:normalize(v) for k,v in x.items() if k not in DROP}
    if isinstance(x,list):return [normalize(v) for v in x]
    if isinstance(x,str) and re.fullmatch(r'0x[0-9a-fA-F]{7,8}',x):return 'pointer'
    return x


def observed(path):
    rows=[json.loads(l) for l in path.read_text().splitlines()]
    meta=[r for r in rows if r['event']=='metadata'];ends=[r for r in rows if r['event']=='trace-end']
    pre=[r for r in rows if r['event']=='preload-file']
    if len(meta)!=1 or meta[0]['sha256']!=SHA or meta[0]['mode']!='observe':raise ValueError('observed provenance')
    if len(ends)!=1 or not ends[0]['installed'] or len(pre)!=1 or not pre[0]['complete']:raise ValueError('incomplete capture')
    if any(r['event'] in ('trace-failed','error') for r in rows):raise ValueError('failed capture')
    markers=[r['value'] for r in rows if r['event']=='marker']
    if not markers or markers[-1]!='G032 tick=650 label=complete':raise ValueError('completion marker')
    for event in EVENTS:
        actual=sum(r['event']==event for r in rows)
        if actual!=ends[0]['counts'].get(event,0):raise ValueError('truncated stream: '+event)
    return rows,meta[0],markers


def verify(first,repeat,control,preload):
    a,ma,markers=observed(first);b,mb,other=observed(repeat)
    for key in ('sha256','source_sha256','map'):
        if ma[key]!=mb[key]:raise ValueError('repeat inputs')
    sa=[normalize(r) for r in a if r['event'] in EVENTS]
    if sa!=[normalize(r) for r in b if r['event'] in EVENTS]:raise ValueError('semantic repeat')
    rows=[json.loads(l) for l in control.read_text().splitlines()];meta=[r for r in rows if r['event']=='metadata']
    pre=[r for r in rows if r['event']=='preload-file']
    if len(meta)!=1 or meta[0]['mode']!='control' or meta[0]['sha256']!=SHA:raise ValueError('control provenance')
    if any(r['event'] in EVENTS|{'module','trace-end','trace-failed','error'} for r in rows):raise ValueError('control hooks/failure')
    if len(pre)!=1 or not pre[0]['complete']:raise ValueError('incomplete control')
    raw=preload.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=pre[0]['sha256']:raise ValueError('control file')
    if re.findall(r'call Preload\( "(G032 [^"\r\n]*)" \)',raw.decode())!=markers:raise ValueError('control timeline')
    value=lambda w:struct.unpack('<f',struct.pack('<I',w))[0]
    adjusted=0;known=0;exempt_adjusted=0
    for r in a:
        if r['event']!='commit' or r.get('speed') is None:continue
        known+=1
        if r['speedHeading']!=r['heading']:raise ValueError('commit heading')
        base=r['req'] if value(r['req'])<value(r['cap']) else r['cap']
        if r['speed']==base:continue
        t=r.get('target')
        if not(int(r['gflags'],16)&0x800) or r['unseen'] or not t or not(value(base)>value(t['max'])):raise ValueError('adjustment prerequisites')
        if not any(w&0x7fffffff for w in t['vel']):raise ValueError('stationary target adjusted')
        if r['speed']!=multiply(t['max'],0x3f733334):raise ValueError('target scale')
        adjusted+=1;exempt_adjusted+=bool(r['d8']&0x1000000)
    if not adjusted or not known:raise ValueError('no speed policy coverage')
    return dict(binary_sha256=SHA,passed=True,commits=sum(r['event']=='commit' for r in a),
                observed_speed_words=known,adjusted=adjusted,exempt_adjusted=exempt_adjusted,
                markers=len(markers),repeats=2,semantic_sha256=hashlib.sha256(json.dumps(sa,sort_keys=True).encode()).hexdigest(),
                input_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (first,repeat,control,preload)},
                scope=__doc__)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('first','repeat','control','preload','report'):ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--expected',type=Path)
    args=ap.parse_args();result=verify(args.first,args.repeat,args.control,args.preload)
    if args.expected and result!=json.loads(gzip.decompress(args.expected.read_bytes())):raise ValueError('frozen witness differs')
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
