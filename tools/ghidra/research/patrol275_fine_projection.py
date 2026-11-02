#!/usr/bin/env python3
"""Patrol275 observed fine search: preserve numeric inputs except declared query-stamp scope."""
import argparse,collections,copy,hashlib,json,pathlib,re
import patrol274_steering_projection as prior
base=prior.base
ROOT=base.ROOT
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_patrol275_fine.json'
EXTRA={'fine-request-enter','fine-request-leave','fine-build-enter','fine-build-leave','fine-cell'}
def norm(rows):
 ptrs={};out=[]
 def pointer(v):
  if v in ('0x0','null'):return 'null'
  if v not in ptrs:ptrs[v]='p'+str(len(ptrs))
  return ptrs[v]
 def walk(x,k=''):
  # Spatial+38 is mutable query visit stamp, not agent identity. Raw values remain in captures and difference audit.
  if k=='eligibility':return {'retired':x==0xffffffff}
  if isinstance(x,str) and (x.startswith('0x') or re.fullmatch(r'p[0-9]+',x) or x=='null'):
   assert k in {'pointer','mover','source','target','association','bridge','receiver','ability','path','target_region','region','fine'},k
   return pointer(x)
  if isinstance(x,dict):return {k:walk(v,k) for k,v in x.items()}
  if isinstance(x,list):
   if k=='packet':
    # Packet+0/+4 opaque words retain literal; +12 retained order/task pointer is relocation only.
    return x[:3]+[pointer(hex(x[3]) if isinstance(x[3],int) else x[3])]
   return [walk(v,k) for v in x]
  return x
 last_scene=object()
 for row in rows:
  if row.get('scene')!=last_scene:ptrs.clear();last_scene=row.get('scene')
  out.append(walk(row))
 return out

def project(capture_dir):
 r=pathlib.Path(capture_dir);base.r=r
 _,a=base.read('observe1');_,b=base.read('observe2');rows=norm(a);assert rows==norm(b),'repeat differs outside declared pointer/query-stamp scope'
 old=json.loads(prior.FIXTURE.read_text());assert norm([z for z in a if z['event'] not in EXTRA])==norm(old['records']),'entire274baseline differs'
 markers=[z['value'] for z in a if z['event']=='marker'];assert markers==old['public']
 c=[json.loads(l) for l in (r/'control1.jsonl').open()];assert {z['event'] for z in c}<={'metadata','owned-loading-input','loading-key','control-start-file','preload-file'}
 assert c[0]['mode']=='control' and any(z.get('event')=='preload-file' and z.get('complete') for z in c)
 assert re.findall(r'PTR [^"\r\n]*',(r/'control1-preload.txt').read_text())==markers
 freeze=json.loads((r/'frozen-sources.json').read_text());assert all(hashlib.sha256((r/k).read_bytes()).hexdigest()==v for k,v in freeze.items())
 assert len(rows)==14890 and len(markers)==1084
 return {'schema':1,'original_sha256':old['original_sha256'],'records':rows,'public':markers,'probe':(r/'itemdrop_probe.j').read_text(),'baseline_records':7542,'limitations':['Explicit pointer relocation is per completed-reset publicscene, preserving aliases within each scene. Canonical identities remain literal.','Spatialregion+38 is a query-dedup visit stamp. Only retired ffffffff classification is compared; all raw numbers retained in capture files and independent difference inventory.','No other numeric word/record/order normalization. Full original274 and265 baseline streams are preserved.','Additional fine search scope is scene3 first request counter1432; no broader search coverage claimed.','Initial Cfixture flatWPM was wrong; original265 map includes fullheight wall atcolumns20..23. No fineplanner/turngate correction inferred.']}

def mutation_checks(d):
 tests=prior.mutation_checks(d);rows=d['records']
 for label,key in [('obstruction','obstruction'),('target-seen','target_seen')]:
  q=copy.deepcopy(rows);next(z for z in q if z['event']=='fine-cell')['after'][key]^=1;assert q!=rows;tests.append(label)
 q=copy.deepcopy(rows);next(z for z in q if z['event']=='fine-request-leave')['after']['indices'][0]^=1;assert q!=rows;tests.append('fine-index')
 q=copy.deepcopy(rows);next(z for z in q if z['event']=='fine-cell')['after']['target_region_state']['eligibility']['retired']=True;assert q!=rows;tests.append('target-retired')
 return tests

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--capture-dir',required=True);ap.add_argument('--check',action='store_true');args=ap.parse_args()
 d=project(args.capture_dir);assert d==json.loads(FIXTURE.read_text()),'missing/extra/reordered/changed portable record'
 p=json.loads(FIXTURE.with_suffix('.provenance.json').read_text())
 for k,v in p['captures'].items():assert hashlib.sha256((pathlib.Path(args.capture_dir)/k).read_bytes()).hexdigest()==v,('capture',k)
 for k,v in p['repository_files'].items():assert hashlib.sha256((ROOT/k).read_bytes()).hexdigest()==v,('repository',k)
 print(json.dumps({'status':'PASS','records':len(d['records']),'public':len(d['public']),'original274_exact_in_declared_scope':True,'negative_controls':len(mutation_checks(d))}))
if __name__=='__main__':main()
