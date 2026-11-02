#!/usr/bin/env python3
"""Controlled native Patrol265 producer replay; exact rows, provenance and mutation controls."""
import json,pathlib,hashlib,collections,re,copy,argparse
ROOT=pathlib.Path(__file__).resolve().parents[3]
FIXTURE=ROOT/"games/warcraft-3/game/tests/fixtures/retail_patrol265_public.json"
r=None

def read(name):
 a=[json.loads(l) for l in (r/(name+'.jsonl')).open()]
 assert not any(x.get('type')=='error' for x in a)
 meta=a[0];freeze=json.loads((r/'frozen-sources.json').read_text());env=meta['env']
 assert env in ('B','C') and meta['owned'] and meta['sha256']=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
 assert meta['display']=={'B':':98','C':':99'}[env] and meta['remote']=={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[env]
 assert meta['source_sha256']=={**{n:freeze[n] for n in ('itemdrop_probe.j','itemdrop_observer.js','itemdrop_capture.py','itemdrop_make_map.py')},'map':freeze['RS-TARGET265-patrol-public-v2.w3m']}
 footer=next(x for x in a if x.get('event')=='trace-end');counts=collections.Counter(x.get('event') for x in a if x.get('event') in footer['counts']);assert dict(counts)==footer['counts']
 assert any(x.get('event')=='trace-end' and x.get('installed') for x in a)
 start=next(i for i,x in enumerate(a) if x.get('event')=='marker' and ' label=start' in x['value'])
 end=next(i for i,x in enumerate(a) if x.get('event')=='marker' and ' label=complete' in x['value'])
 return a,[x for x in a[start:end+1] if x.get("event") not in ("owned-loading-input","loading-key")]

def norm(rows):
 ptrs={};out=[]
 def pointer(v):
  if v=='0x0':return 'null'
  if v not in ptrs:ptrs[v]='p'+str(len(ptrs))
  return ptrs[v]
 def walk(x,k=''):
  if isinstance(x,str) and x.startswith('0x'):
   assert k in {'pointer','mover','source','target','association','bridge','receiver','ability'},k
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

def project(capture_dir):
 global r
 r=pathlib.Path(capture_dir)
 a,x=read('observe1');b,y=read('observe2');xn,yn=norm(x),norm(y)
 if xn!=yn:
  diff=next(((i,u,v) for i,(u,v) in enumerate(zip(xn,yn)) if u!=v),None)
  raise AssertionError(('repeat',len(xn),len(yn),diff))
 markers=[z['value'] for z in x if z['event']=='marker'];assert not any('ERROR' in v for v in markers)
 c=[json.loads(l) for l in (r/'control1.jsonl').open()];assert {z['event'] for z in c}<={'metadata','owned-loading-input','loading-key','control-start-file','preload-file'};assert any(z.get('event')=='preload-file' and z.get('complete') for z in c);assert c[0]['mode']=='control'
 text=(r/'control1-preload.txt').read_text();cm=re.findall(r'PTR [^"\r\n]*',text);assert cm==markers,(len(cm),len(markers))
 frozen=json.load(open(r/'frozen-sources.json'));assert all(hashlib.sha256((r/k).read_bytes()).hexdigest()==v for k,v in frozen.items())
 setups=[v for v in markers if 'label=setup ' in v];returns=[v for v in markers if 'label=issue-return ' in v];assert len(setups)==len(returns)==12
 # Assert independently reviewed producer facts in addition to the full stream.
 assert [int(re.search(r'result=(\d+)',v)[1]) for v in returns]==[1,1,1,1,1,1,1,1,0,0,1,0]
 assert [int(re.search(r'head=(\d+)',v)[1]) for v in returns]==[0,851991,851991,851990,851990,851991,851991,0,0,0,851991,0]
 for scene in (1,2,6,10):
  tasks=[z for z in x if z.get('scene')==scene and z['event'] in ('point-task','two-point-task')]
  assert tasks and all(z['range']==0 and z['association']=='0x0' for z in tasks)
 for scene in (3,4):
  tasks=[z for z in x if z.get('scene')==scene and z['event']=='target-task']
  assert [(z['code'],z['range']) for z in tasks]==[(0xd0193,0),(0xd0173,0x43960000),(0xd0193,0),(0xd0174,0x43960000)]
 # Generic full-stream comparator must reject missing, extra, reordered and changed raw fields.
 mutants={}
 mutants['missing']=copy.deepcopy(xn);mutants['missing'].pop(5)
 mutants['extra']=copy.deepcopy(xn);mutants['extra'].insert(5,copy.deepcopy(xn[5]))
 mutants['reordered']=copy.deepcopy(xn);mutants['reordered'][5:7]=reversed(mutants['reordered'][5:7])
 for label,event,key in [('arrival-angle','arrival','angle'),('arrival-result','arrival','result'),('velocity-speed','velocity','speed'),('range','point-task','range'),('task-code','target-task','code')]:
  m=copy.deepcopy(xn);z=next(z for z in m if z['event']==event);z[key]^=1;mutants[label]=m
 m=copy.deepcopy(xn);next(z for z in m if z['event']=='marker')['value']+=' altered';mutants['public']=m
 assert all(m!=xn for m in mutants.values())

 summary={'negative_controls':list(mutants),'records':len(x),'public':len(markers),'counts':dict(collections.Counter(z['event'] for z in x)),'setups':setups,'returns':returns,'projection_sha256':hashlib.sha256(json.dumps(xn,separators=(',',':')).encode()).hexdigest(),'public_sha256':hashlib.sha256(json.dumps(markers,separators=(',',':')).encode()).hexdigest(),'scope':'start through declaredcomplete; raw after-complete shutdown tail retained, excluded by fixed public boundary'}
 return {'stock_profiles':json.loads((r/'stock-profiles.json').read_text()),'schema':1,'original_sha256':'d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236','summary':summary,'records':xn,'public':markers,'probe':(r/'itemdrop_probe.j').read_text(),'limitations':json.loads((r/'findings.json').read_text())['limitations']}

def typed_polls(records):
 roles={};out=[]
 for x in records:
  if x['event'] not in ('arrival','velocity'):continue
  key=(x['scene'],x['before']['mover'])
  if key not in roles:
   xy=x['before']['pose'][2:4];assert xy[1]==0x41800000 and xy[0] in (0x41800000,0x42000000)
   roles[key]=0 if xy[0]==0x41800000 else 1
  if x['event']=='arrival':phase=0;words=[x['clock']['primary'][0],*x['source'],*x['destination'],x['heading'],x['threshold'],x['angle'],x['inside'],x['result']]
  else:phase=2;words=[x['clock']['primary'][0],*x['after']['pose'][2:6],x['after']['heading']]
  out.append({'scene':x['scene'],'phase':phase,'role':roles[key],'words':words})
 assert len(out)==1632
 return out

def public_header(d):
 s='#ifndef RETAIL_PATROL265_PUBLIC_H\n#define RETAIL_PATROL265_PUBLIC_H\n'
 s+='/* Native typed FourCC marker values use retail big-endian integer encoding. */\n'
 s+='static char const *const patrol_native_public265[] = {\n'
 for line in d['public']:s+='    '+json.dumps(line)+',\n'
 s+='};\nstatic char const patrol_native_probe265[] =\n'
 for line in d['probe'].splitlines(keepends=True):s+='    '+json.dumps(line)+'\n'
 s+=';\ntypedef struct { unsigned scene,phase,role; uint32_t words[10]; } patrolNativePoll265_t;\n'
 s+='static patrolNativePoll265_t const patrol_native_polls265[] = {\n'
 for x in typed_polls(d['records']):s+='{%d,%d,%d,{%s}},\n'%(x['scene'],x['phase'],x['role'],','.join('0x%08xu'%v for v in x['words']))
 return s+'};\n#endif\n'

def stock_header(d):
 head='#ifndef RETAIL_PATROL265_STOCK_H\n#define RETAIL_PATROL265_STOCK_H\n/* Effective original1.27 stock hfoo+hbar; fullrows retained in portable provenance. */\n'
 for table,rows in d['stock_profiles']['effective'].items():
  cols=list(dict.fromkeys(k for row in rows.values() for k in row));text='ID;PWXL;N;E\n'
  for i,k in enumerate(cols,1):text+=f'C;X{i};Y1;K"{k}"\n'
  for y,row in enumerate(rows.values(),2):
   for x,k in enumerate(cols,1):
    if k in row:text+=f'C;X{x};Y{y};K"{row[k]}"\n'
  text+='E\n';head+='static char const patrol265_'+table+'[] =\n'+''.join('    '+json.dumps(l)+'\n' for l in text.splitlines(keepends=True))+';\n'
 return head+'#endif\n'

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--capture-dir',required=True);ap.add_argument('--check',action='store_true');args=ap.parse_args()
 actual=project(args.capture_dir);expected=json.loads(FIXTURE.read_text());assert actual==expected,'portable projection differs (missing/extra/reordered/changed row)'
 provenance=json.loads(FIXTURE.with_suffix('.provenance.json').read_text())
 for name,digest in provenance['captures'].items():assert hashlib.sha256((pathlib.Path(args.capture_dir)/name).read_bytes()).hexdigest()==digest,('capture provenance',name)
 for name,digest in provenance['repository_files'].items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,('repository provenance',name)
 stock=FIXTURE.parent/'retail_patrol265_stock.h';assert stock.read_text()==stock_header(expected),'semantic stock profile header mismatch'
 header=FIXTURE.with_suffix('.h')
 if header.exists():assert header.read_text()==public_header(expected),'semantic public header mismatch'
 print(json.dumps({'status':'PASS','records':len(actual['records']),'public':len(actual['public']),'negative_controls':len(actual['summary']['negative_controls']),'header_checked':header.exists()}))
if __name__=='__main__':main()
