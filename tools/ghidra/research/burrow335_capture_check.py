#!/usr/bin/env python3
"""Literal native Burrow335 v1/v2 audit. No engine parity or pointer normalization."""
import argparse,base64,collections,copy,gzip,hashlib,json,re
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
DEFAULT=Path(__file__).resolve().parents[1]/'fixtures/research/burrow335-v2-public-original.json'
sha=lambda b:hashlib.sha256(b).hexdigest()
def audit(files,spec):
 assert spec['version'] in (1,2)
 assert spec['public_records']=={1:1416,2:1290}[spec['version']]
 freeze=json.loads(files['source-freeze.json'])
 for p,h in spec['readable_sources'].items():
  assert sha(files[p])==h==freeze['tools/frida/research/'+p.split('/')[-1]]
 streams=[];counts=[]
 for name in ['observe','repeat','control']:
  rows=[json.loads(x) for x in files[name+'.jsonl'].decode().splitlines()]
  meta=rows[0];assert meta['source_sha256']==spec['capture_source_sha256']
  assert meta['mode']==('control' if name=='control' else 'observe')
  assert meta['sha256']=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
  end=[r for r in rows if r['event']=='source-end'];assert len(end)==1 and end[0]['unchanged'] and end[0]['source_sha256']==meta['source_sha256']
  assert meta['source_sha256']['map']==spec['map_sha256']
  assert not any(r.get('event') in ['error','native-exception','trace-failed'] or r.get('type')=='error' for r in rows)
  public=[]
  for suffix,event in [('-start-preload.txt','start-preload-file'),('-preload.txt','preload-file')]:
   b=files[name+suffix];r=[r for r in rows if r['event']==event];assert len(r)==1 and r[0]['sha256']==sha(b)
   lines=re.findall(r'call Preload\( "(B335 [^"\r\n]*)" \)',b.decode());assert len(lines)==(1 if 'start' in suffix else (spec['public_records']-1))
   if 'start' not in suffix:assert r[0]['complete'] and r[0]['markers']==(spec['public_records']-1)
   public+=lines
  assert len(public)==spec['public_records'] and public[-1]=='B335 tick=200 scene=2 label=complete'
  for scene in range(2):
   subset=[s for s in public if f' scene={scene} ' in s]
   assert any('label=sleep result=1' in s for s in subset)
   assert any('sleep=1 guard=1' in s for s in subset)
   assert any('paused=1' in s for s in subset)
   assert any('base=0 alternate=1' in s for s in subset)
  if name!='control':
   native=[r for r in rows if 'seq' in r];f=[r for r in rows if r['event']=='trace-end'];assert len(f)==1;f=f[0]
   assert f['complete'] and f['installed'] and f['readOnly'] and f['dropped']==0 and not f['overflow'] and f['pending_queries']==0
   assert [r['seq'] for r in native]==list(range(1,len(native)+1)) and f['sequence']==len(native)
   assert dict(collections.Counter(r['event'] for r in native))==f['counts']
   assert [r['value'] for r in native if r['event']=='marker']==public
   counts.append(f['counts'])
  else:assert set(r['event'] for r in rows)<={'metadata','owned-loading-input','loading-key','control-start-file','start-preload-file','preload-file','source-end'}
  if spec['version']==2:
   for scene,buff in [(0,'inv'),(1,'ww')]:
    assert any(f' scene={scene} ' in v and 'label=state' in v and f'{buff}=1' in v and 'burrow=1' in v and 'sleep=1 guard=1' in v for v in public)
    assert any(f'tick={35+scene*100} scene={scene} label=state inv=0 sleep=1 guard=1 burrow=0 ww=0' in v for v in public)
    assert any(f'tick={35+scene*100} scene={scene} label=type base=1 alternate=0' in v for v in public)
  if spec['version']==1:
   for scene,offset in [(0,0),(1,100)]:
    assert any(f'tick={16+offset} scene={scene} label=state inv=0 sleep=0 guard=0 burrow=1 ww=0' in v for v in public)
  # Preserve every private row and original identity/scalar word, independently per capture.
  if name!='control':
   assert sha(json.dumps(native,sort_keys=True,separators=(',',':')).encode())==spec['native_rows_sha256'][name]
   ww=[r for r in native if r['event']=='windwalk-setup'];assert len(ww)==1
   assert (ww[0]['duration'],ww[0]['fade'],ww[0]['speed'])==(0x41400000,0x3f400000,0x3e800000)
  streams.append(public)
 assert streams[0]==streams[1]==streams[2]
 assert counts[0]==counts[1]
 assert sha(('\n'.join(streams[0])+'\n').encode())==spec['public_sha256']
 return streams[0],counts

def load(path):
 spec=json.loads(path.read_text());blob=path.with_suffix('.json.gz').read_bytes()
 assert sha(blob)==spec['bundle_sha256']
 files={n:base64.b64decode(v,validate=True) for n,v in json.loads(gzip.decompress(blob))['files'].items()}
 assert set(files)==set(spec['files'])
 for n,b in files.items():assert sha(b)==spec['files'][n]
 return files,spec

def mutations(files,spec):
 def change(event,field,value):
  return lambda rows:next(r for r in rows if r['event']==event).__setitem__(field,value)
 cases=[change('metadata','sha256','0'*64),change('metadata','mode','control'),change('source-end','unchanged',False),change('trace-end','dropped',1),change('trace-end','sequence',0),change('windwalk-setup','fade',0),lambda r:next(x for x in r if x['event']=='unit-state')['unit']['identity'].__setitem__(1,0),lambda r:next(x for x in r if x['event']=='unit-state')['unit'].__setitem__('invisibility',999),lambda r:next(x for x in r if x['event']=='marker' and 'sleep=1 guard=1' in x['value']).__setitem__('value','B335 invalid Sleep guard'),lambda r:next(x for x in r if x['event']=='marker' and 'paused=1' in x['value']).__setitem__('value','B335 invalid Pause')]
 for i,mutation in enumerate(cases):
  altered=dict(files);rows=[json.loads(x) for x in files['observe.jsonl'].splitlines()];mutation(rows)
  altered['observe.jsonl']=('\n'.join(json.dumps(x) for x in rows)+'\n').encode()
  try:audit(altered,spec)
  except (AssertionError,ValueError,KeyError):continue
  raise ValueError('accepted mutation '+str(i))
 return len(cases)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--spec',type=Path,default=DEFAULT);p.add_argument('--check',action='store_true');p.add_argument('--write-public',type=Path);p.add_argument('--binary',type=Path);a=p.parse_args()
 files,spec=load(a.spec)
 if a.binary:assert sha(a.binary.read_bytes())==SHA
 public,counts=audit(files,spec);negative=mutations(files,spec) if a.check else 0
 if a.write_public:
  with a.write_public.open('x') as f:f.write('\n'.join(public)+'\n')
 print(json.dumps(dict(passed=True,public_per_capture=len(public),native_counts=counts,negative_controls=negative,engine_parity=False)))
if __name__=='__main__':main()
