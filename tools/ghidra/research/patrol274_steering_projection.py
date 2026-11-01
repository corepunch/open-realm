#!/usr/bin/env python3
"""Exact controlled Patrol274 steering extension; no inferred path-cache fields."""
import argparse,collections,copy,hashlib,json,pathlib,re
import patrol265_public_projection as base
ROOT=base.ROOT
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_patrol274_steering.json'
EXTRA={'step-route-enter','step-route-leave','path-advance-enter','path-advance-leave','motion-gate-enter','motion-gate-leave'}
def norm(rows):
 ptrs={};out=[]
 def pointer(v):
  if v=='0x0':return 'null'
  if v not in ptrs:ptrs[v]='p'+str(len(ptrs))
  return ptrs[v]
 def walk(x,k=''):
  if isinstance(x,str) and x.startswith('0x'):
   assert k in {'pointer','mover','source','target','association','bridge','receiver','ability','path','target_region'},k
   return pointer(x)
  if isinstance(x,dict):return {k:walk(v,k) for k,v in x.items()}
  if isinstance(x,list):
   if k=='packet':
    # Packet+0/+4 opaque words retain literal; +12 retained order/task pointer is relocation only.
    return x[:3]+[pointer(hex(x[3]))]
   return [walk(v,k) for v in x]
  return x
 for row in rows:out.append(walk(row))
 return out

def legacy(v):
 if isinstance(v,dict):return {k:legacy(q) for k,q in v.items() if k!='motion_parameters'}
 if isinstance(v,list):return [legacy(q) for q in v]
 return v

def project(capture_dir):
 r=pathlib.Path(capture_dir);base.r=r
 _,a=base.read('observe1');_,b=base.read('observe2');rows=norm(a);assert rows==norm(b),'extended repeat mismatch'
 prior=json.loads(base.FIXTURE.read_text());assert base.norm([legacy(z) for z in a if z['event'] not in EXTRA])==prior['records'],'entire265baseline differs'
 markers=[z['value'] for z in a if z['event']=='marker'];assert markers==prior['public']
 control=[json.loads(l) for l in (r/'control1.jsonl').open()];assert {z['event'] for z in control}<={'metadata','owned-loading-input','loading-key','control-start-file','preload-file'}
 assert control[0]['mode']=='control' and any(z.get('event')=='preload-file' and z.get('complete') for z in control)
 assert re.findall(r'PTR [^"\r\n]*',(r/'control1-preload.txt').read_text())==markers
 freeze=json.loads((r/'frozen-sources.json').read_text());assert all(hashlib.sha256((r/k).read_bytes()).hexdigest()==v for k,v in freeze.items())
 assert len(rows)==7542 and len(markers)==1084
 return {'schema':1,'original_sha256':prior['original_sha256'],'records':rows,'public':markers,'probe':(r/'itemdrop_probe.j').read_text(),'baseline_records':2986,'limitations':['Pointer relocation only; all numeric words and global record order remain literal.','Declared start through complete; all shutdown tails remain in immutable raw captures.','No native fine table/index or obstruction-latch observation in this version.','No engine parity claim.']}

def mutation_checks(d):
 rows=d['records'];tests=[]
 for kind in ('missing','extra','reorder'):
  q=copy.deepcopy(rows)
  if kind=='missing':q.pop(5)
  elif kind=='extra':q.insert(5,copy.deepcopy(q[5]))
  else:q[5:7]=reversed(q[5:7])
  assert q!=rows;tests.append(kind)
 for event,key in [('arrival','angle'),('arrival','result'),('velocity','speed'),('point-task','range'),('target-task','code'),('motion-gate-enter','error'),('motion-gate-enter','stop'),('path-advance-leave','result')]:
  q=copy.deepcopy(rows);next(z for z in q if z['event']==event)[key]^=1;assert q!=rows;tests.append(event+':'+key)
 for event,key,index in [('path-advance-leave','waypoint',0),('motion-gate-enter','motion_parameters',2)]:
  q=copy.deepcopy(rows);z=next(z for z in q if z['event']==event)
  if key=='motion_parameters':z=z['before']
  z[key][index]^=1;assert q!=rows;tests.append(event+':'+key)
 q=copy.deepcopy(rows);next(z for z in q if z['event']=='marker')['value']+=' altered';assert q!=rows;tests.append('public')
 return tests

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--capture-dir',required=True);ap.add_argument('--check',action='store_true');args=ap.parse_args()
 d=project(args.capture_dir);assert d==json.loads(FIXTURE.read_text()),'missing/extra/reordered/changed portable record'
 p=json.loads(FIXTURE.with_suffix('.provenance.json').read_text())
 for k,v in p['captures'].items():assert hashlib.sha256((pathlib.Path(args.capture_dir)/k).read_bytes()).hexdigest()==v,('capture',k)
 for k,v in p['repository_files'].items():assert hashlib.sha256((ROOT/k).read_bytes()).hexdigest()==v,('repository',k)
 print(json.dumps({'status':'PASS','records':len(d['records']),'public':len(d['public']),'original265_exact':True,'negative_controls':len(mutation_checks(d))}))
if __name__=='__main__':main()
