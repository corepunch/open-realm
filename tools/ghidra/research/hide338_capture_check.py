#!/usr/bin/env python3
"""Strict literal original338 Hide/ShadowMeld disposition; no engine parity."""
import argparse,base64,collections,gzip,hashlib,json,re
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
DEFAULT=Path(__file__).resolve().parents[1]/'fixtures/research/hide338-public-original.json'
sha=lambda b:hashlib.sha256(b).hexdigest()
def audit(files,spec):
 assert spec['public_records']==832 and spec['native_records']==1107
 freeze=json.loads(files['source-freeze.json'])
 for p,h in spec['readable_sources'].items():assert sha(files[p])==h==freeze['tools/frida/research/'+p.split('/')[-1]]
 streams=[];natives=[];projections=[]
 for name in ['observe','repeat','control']:
  rows=[json.loads(x) for x in files[name+'.jsonl'].decode().splitlines()];m=rows[0];assert m['source_sha256']==spec['capture_source_sha256']
  assert m['mode']==('control' if name=='control' else 'observe') and m['sha256']=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
  assert m['source_sha256']['map']==spec['map_sha256']
  end=[x for x in rows if x['event']=='source-end'];assert len(end)==1 and end[0]['unchanged'] and end[0]['source_sha256']==m['source_sha256']
  assert not any(x.get('type')=='error' or x['event'] in ['error','native-exception','trace-failed'] for x in rows)
  public=[]
  for suffix,event,size in [('-start-preload.txt','start-preload-file',1),('-preload.txt','preload-file',831)]:
   b=files[name+suffix];f=[x for x in rows if x['event']==event];assert len(f)==1 and f[0]['sha256']==sha(b)
   lines=re.findall(r'call Preload\( "(H338 [^"\r\n]*)" \)',b.decode());assert len(lines)==size;public+=lines
   if size==831:assert f[0]['complete'] and f[0]['markers']==size
  assert public[-1]=='H338 tick=200 scene=4 label=complete' and len(public)==832
  assert not any('label=spell ' in x for x in public)
  for scene in range(4):
   subset=[x for x in public if f' scene={scene} ' in x]
   assert not any('Apiv=1' in x for x in subset)
   assert any('label=ambush result=1' in x for x in subset)
   assert any('label=remove-Ahid result='+('0' if scene==2 else '1') in x for x in subset)
   assert any('label=move result=1' in x for x in subset)
   if scene<2:assert not any('Ashm=1' in x for x in subset)
  if name=='control':assert set(x['event'] for x in rows)<={'metadata','owned-loading-input','loading-key','control-start-file','start-preload-file','preload-file','source-end'}
  else:
   n=[x for x in rows if 'seq' in x];f=[x for x in rows if x['event']=='trace-end'];assert len(f)==1;f=f[0]
   assert f['complete'] and f['readOnly'] and f['installed'] and not f['dropped'] and not f['overflow'] and f['pending_queries']==0
   assert [x['seq'] for x in n]==list(range(1,1108)) and f['sequence']==1107 and f['counts']==dict(collections.Counter(x['event'] for x in n))
   assert [x['value'] for x in n if x['event']=='marker']==public
   state=[x for x in n if x['event']=='unit-state']
   assert all(x['unit']['invisibility']==0 for x in state if x['scene']<2)
   assert all(any(x['scene']==scene and x['unit']['invisibility']==1 for x in state) for scene in [2,3])
   for scene in range(4):
    assert any(x['scene']==scene and x['tick']==scene*50+21 and x['unit']['attack_suppression']==1 for x in state)
    assert any(x['scene']==scene and x['tick']==scene*50+36 and x['unit']['attack_suppression']==(1 if scene<2 else 0) for x in state)
   projection=[dict(scene=x['scene'],tick=x['tick'],seq=x['seq'],invisibility=x['unit']['invisibility'],attack_suppression=x['unit']['attack_suppression'],flags=x['unit']['flags']) for x in state]
   assert sha(json.dumps(n,sort_keys=True,separators=(',',':')).encode())==spec['native_rows_sha256'][name]
   projections.append(projection);natives.append(f['counts'])
  streams.append(public)
 assert streams[0]==streams[1]==streams[2] and projections[0]==projections[1] and natives[0]==natives[1]
 assert sha(('\n'.join(streams[0])+'\n').encode())==spec['public_sha256']
 return streams[0],natives

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
 cases=[change('metadata','sha256','0'*64),change('metadata','mode','control'),change('source-end','unchanged',False),change('trace-end','dropped',1),change('trace-end','sequence',0),lambda r:next(x for x in r if x['event']=='hide-leave')['ability'].__setitem__('flags',0),lambda r:next(x for x in r if x['event']=='unit-state')['unit']['identity'].__setitem__(1,0),lambda r:next(x for x in r if x['event']=='unit-state')['unit'].__setitem__('invisibility',999),lambda r:next(x for x in r if x['event']=='marker' and 'label=remove-Ahid result=1' in x['value']).__setitem__('value','H338 invalid removal'),lambda r:next(x for x in r if x['event']=='marker' and 'label=abilities' in x['value']).__setitem__('value','H338 invalid membership')]
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
