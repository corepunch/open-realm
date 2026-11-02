#!/usr/bin/env python3
"""Audit frozen original-engine captures; requires locally retained raw evidence."""
import argparse,hashlib,json,re,struct
from pathlib import Path
p=argparse.ArgumentParser(description='Verify native Invisibility324 capture and observer-free control.');p.add_argument('--version',default='v4');p.add_argument('--captures',type=Path,required=True);a=p.parse_args();b=a.captures.resolve()
rows=[json.loads(l)for l in (b/f'observe-{a.version}.jsonl').read_text().splitlines()]
control=[json.loads(l)for l in (b/f'control-{a.version}.jsonl').read_text().splitlines()]
for stream, mode in [(rows,'observe'),(control,'control')]:
 metadata=[r for r in stream if r['event']=='metadata'];assert len(metadata)==1
 assert metadata[0]['mode']==mode and metadata[0]['sha256']=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'

def public(name):return re.findall(r'call Preload\( "(I324 [^"\r\n]*)" \)',(b/name).read_text())
observed=public(f'observe-{a.version}-preload.txt');plain=public(f'control-{a.version}-preload.txt')
assert observed==plain and observed[-1].endswith('label=complete')
assert not any('OVERSIZE' in s for s in observed)
markers=[r['value']for r in rows if r['event']=='marker'];assert markers==[observed[0]]+observed
foot=[r for r in rows if r['event']=='trace-end'];assert len(foot)==1
f=foot[0];assert f['installed'] and f['complete'] and not f['overflow'] and f['dropped']==0 and f['pending_queries']==0
scoped=[r for r in rows if 'seq'in r];assert [r['seq']for r in scoped]==list(range(1,len(scoped)+1))
for name,n in f['counts'].items():assert sum(r['event']==name for r in scoped)==n
for stream in (rows,control):
 assert not any(r['event'] in ('trace-failed','error')for r in stream)
 assert stream[-1]['event']=='source-end' and stream[-1]['unchanged']
 assert [r for r in stream if r['event']=='preload-file'][0]['complete']
 if stream is control:assert not any('seq'in r for r in stream)
preconditions=[s for s in observed if 'result='in s or 'label=inventory'in s]
assert all(s.endswith('=1')for s in preconditions)
def f32(w):return struct.unpack('<f',struct.pack('<I',w))[0]
setups=[dict(seq=r['seq'],last_public_tick=r['tick'],vtable=hex(r['buff']['vtable']),duration_word=r['duration'],duration=f32(r['duration']))for r in rows if r['event']=='buff-enter' and r['rva']==0x532b50]
if a.version=='v4':
 for vt,d in [('0xb3fca8',4.0),('0xb0aa60',12.0),('0xb0a6e4',2.0)]:assert sum(x['vtable']==vt and x['duration']==d for x in setups)==3
 assert sum('label=remove-apiv result=1'in s for s in observed)==2
freeze=json.loads((b/f'freeze-{a.version}.json').read_text())
if a.version=='v4':
 for name,digest in freeze.items():assert hashlib.sha256(Path(name).read_bytes()).hexdigest()==digest
summary={'accepted':True,'version':a.version,'observations':1,'requested_guard_word':1065353216,'effective_guard_word':1073741824,'guard_override_effective':False,'fresh_unhooked_controls':1,'public_records':len(observed),'observer_records':len(scoped),'public_sha256':hashlib.sha256(('\n'.join(observed)+'\n').encode()).hexdigest(),'preconditions':preconditions,'setups':setups,'footer':f,'limits':['One observation plus one fresh control; no private-repeat claim.','Private callback tick is last public marker, not an independent event timestamp.','IsUnitInvisible(Player1) retains inherited map viewer/alliance semantics.','Existing capture-wrapper metadata hashes legacy299 sources; independent freeze manifest pins actual324 inputs.'],'files':{x.name:hashlib.sha256(x.read_bytes()).hexdigest()for x in [b/f'observe-{a.version}.jsonl',b/f'control-{a.version}.jsonl',b/f'Invisibility324-{a.version}.w3m']}}
(b/f'audit-{a.version}.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({k:summary[k]for k in ('accepted','version','public_records','observer_records','public_sha256')}))
