#!/usr/bin/env python3
"""Strict portable native332 capture audit; no engine parity claim."""
import argparse,base64,collections,copy,gzip,hashlib,json,re
from pathlib import Path
DEFAULT=Path(__file__).resolve().parents[1]/'fixtures/research/invisibility332-birth-original.json'
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
def sha(data):return hashlib.sha256(data).hexdigest()
def require(value,message):
 if not value:raise ValueError(message)
def audit(files,spec):
 version=spec.get('version',1)
 require(version in (1,2),'supported capture version')
 public_count,native_count=(145,170) if version==1 else (148,173)
 require(spec['public_records']==public_count,'declared public size')
 require(spec['capture_source_sha256']['map']==spec['map_sha256'],'map pin')
 freeze=json.loads(files['source-freeze.json'])
 for name,digest in spec['readable_sources'].items():
  require(freeze['tools/frida/research/'+name.split('/')[-1]]==digest,'frozen actual source')
 streams=[]; observations=[]
 for name in ('observe','repeat','control'):
  rows=[json.loads(line) for line in files[name+'.jsonl'].splitlines()]
  meta=rows[0];require(meta['event']=='metadata' and meta['sha256']==SHA,'native metadata')
  require(meta['mode']==('control' if name=='control' else 'observe'),'capture mode')
  require(meta['source_sha256']==spec['capture_source_sha256'],'capture source pins')
  end=[r for r in rows if r['event']=='source-end'];require(len(end)==1 and end[0]['unchanged'] and end[0]['source_sha256']==meta['source_sha256'],'source end')
  public=[]
  for suffix,event,count in (('-start-preload.txt','start-preload-file',1),('-preload.txt','preload-file',public_count-1)):
   data=files[name+suffix]; found=[r for r in rows if r['event']==event]
   require(len(found)==1 and found[0]['sha256']==sha(data),'preload hash')
   lines=re.findall(r'call Preload\( "(I332 [^"\r\n]*)" \)',data.decode())
   require(len(lines)==count,'literal preload count');public+=lines
   if count==public_count-1:require(found[0]['complete'] and found[0]['markers']==public_count-1,'preload completion')
  require(len(public)==public_count and public[-1]=='I332 tick=20 scene=0 label=complete','public completion')
  for player in (0,1):
   require(f'I332 tick=0 scene=0 label=point-p{player} stage=before-source visible=0 fogged=0' in public,'before-source masked point')
   for stage in ('after-source','after-caster'):
    require(f'I332 tick=0 scene=0 label=unit-p{player} stage={stage} visible=1 invisible=0' in public,'synchronous unit birth visibility')
    require(f'I332 tick=0 scene=0 label=point-p{player} stage={stage} visible=1 fogged=0' in public,'synchronous point birth visibility')
   for tick in range(1,21):
    stage='complete' if tick==20 else 'periodic'
    require(f'I332 tick={tick} scene=0 label=unit-p{player} stage={stage} visible=1 invisible=0' in public,'periodic unit visibility')
    require(f'I332 tick={tick} scene=0 label=point-p{player} stage={stage} visible=1 fogged=0' in public,'periodic point visibility')
  before=public.index('I332 tick=0 scene=0 label=point-p1 stage=before-source visible=0 fogged=0')
  after=public.index('I332 tick=0 scene=0 label=point-p1 stage=after-source visible=1 fogged=0')
  caster=public.index('I332 tick=0 scene=0 label=point stage=before-caster x=1400.000 y=512.000')
  require(before<after<caster,'source birth precedes caster creation')
  if version==2:
   placed=[x for x in public if 'label=point stage=before-placed-cell ' in x]
   source=[x for x in public if 'label=point stage=after-source ' in x]
   require(len(placed)==len(source)==1,'single same-cell query')
   xy=lambda line:re.search(r'x=([^ ]+) y=([^ ]+)$',line).groups()
   pose=public[public.index(next(x for x in public if 'label=unit-p0 stage=after-source ' in x))-1]
   require(xy(placed[0])==xy(source[0])==xy(pose),'actual placed-cell coordinates')
   for player in (0,1):
    masked=f'I332 tick=0 scene=0 label=point-p{player} stage=before-placed-cell visible=0 fogged=0'
    visible=f'I332 tick=0 scene=0 label=point-p{player} stage=after-source visible=1 fogged=0'
    require(masked in public and public.index(placed[0])<public.index(masked)<public.index(pose)<public.index(visible)<caster,'same-cell birth publication order')
  require(not any(r['event'] in ('error','native-exception','trace-failed') or r.get('type')=='error' for r in rows),'capture errors')
  if name=='control':
   require(set(r['event'] for r in rows)<= {'metadata','owned-loading-input','loading-key','control-start-file','start-preload-file','preload-file','source-end'},'unhooked control')
  else:
   native=[r for r in rows if 'seq' in r];footer=[r for r in rows if r['event']=='trace-end']
   require(len(footer)==1,'native footer');f=footer[0]
   require(f['installed'] and f['complete'] and f['readOnly'] and f['dropped']==0 and not f['overflow'] and f['pending_queries']==0,'native completion')
   require([r['seq'] for r in native]==list(range(1,native_count+1)) and f['sequence']==native_count,'ordered native sequence')
   require(dict(collections.Counter(r['event'] for r in native))==f['counts']==spec['native_counts'],'footer counts')
   require([r['value'] for r in native if r['event']=='marker']==public,'hook/public equivalence')
   recovery=[r for r in native if r['event']=='recovery-enter']
   require(len(recovery)==2 and all(r['return_rva']==0x49cecb and r['forced']==0 and r['capture']==0 and not r['ability']['flags']&4 for r in recovery),'inactive initialization recovery')
   observations.append(len(native))
  streams.append(public)
 require(streams[0]==streams[1]==streams[2],'literal repeat/control equality')
 require(sha(('\n'.join(streams[0])+'\n').encode())==spec['public_sha256'],'frozen complete public stream')
 return streams[0],observations

def load(path):
 spec=json.loads(path.read_text());blob=path.with_suffix('.json.gz').read_bytes()
 require(sha(blob)==spec['bundle_sha256'],'compressed archive pin')
 bundle=json.loads(gzip.decompress(blob));files={n:base64.b64decode(v,validate=True) for n,v in bundle['files'].items()}
 require(set(files)==set(spec['files']),'archive inventory')
 for name,data in files.items():require(sha(data)==spec['files'][name],'literal file '+name)
 for name,digest in spec['readable_sources'].items():require(sha(files[name])==digest,'source bytes')
 return files,spec

def mutations(files,spec):
 def change(event,field,value):
  def mutation(rows):next(r for r in rows if r['event']==event)[field]=value
  return mutation
 cases=[change('metadata','sha256','0'*64),change('marker','value','I332 tick=0 scene=0 label=corrupt'),change('source-end','unchanged',False),change('trace-end','dropped',1),change('trace-end','complete',False),change('trace-end','sequence',169),lambda rows:next(r for r in rows if r['event']=='recovery-enter')['ability'].__setitem__('flags',4),change('recovery-enter','capture',1)]
 for i,mutation in enumerate(cases):
  altered=dict(files);rows=[json.loads(line) for line in files['observe.jsonl'].splitlines()];mutation(rows)
  altered['observe.jsonl']=('\n'.join(json.dumps(r) for r in rows)+'\n').encode()
  try:audit(altered,spec)
  except (ValueError,KeyError):continue
  raise ValueError('mutation accepted '+str(i))
 return len(cases)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--spec',type=Path,default=DEFAULT);p.add_argument('--check',action='store_true');p.add_argument('--write-public',type=Path);p.add_argument('--binary',type=Path);a=p.parse_args()
 files,spec=load(a.spec)
 if a.binary:require(sha(a.binary.read_bytes())==SHA,'original binary SHA')
 public,counts=audit(files,spec);negative=mutations(files,spec) if a.check else 0
 if a.write_public:
  with a.write_public.open('x') as f:f.write('\n'.join(public)+'\n')
 print(json.dumps(dict(passed=True,public_per_capture=len(public),native_per_observation=counts,negative_controls=negative,engine_parity=False)))
if __name__=='__main__':main()
