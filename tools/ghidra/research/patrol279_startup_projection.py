#!/usr/bin/env python3
"""Exact native265 purpose setup and Attack reaction inputs; no engine inference."""
import argparse,pathlib,json,hashlib,collections,re,copy
import patrol275_fine_projection as prior
ROOT=prior.ROOT
FIXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_patrol279_startup.json'
EXTRA={'streams-reseed-enter', 'attack-timer-request', 'reaction-config', 'reaction-enter', 'attack-attach-enter', 'streams-reseed-leave', 'attack-scan-leave', 'attack-timer-return', 'reaction-leave', 'attack-scan-enter', 'stream-real', 'random-seed', 'attack-attach-leave'}
def normalized(rows):
 ptrs={}
 def ptr(v):
  if v=='0x0':return 'null'
  if v not in ptrs:ptrs[v]='p'+str(len(ptrs))
  return ptrs[v]
 def walk(v,k=''):
  if isinstance(v,dict):return {a:walk(b,a) for a,b in v.items()}
  if isinstance(v,list):
   if k=='request_words':return [ptr(hex(x)) if i in (3,6) else x for i,x in enumerate(v)]
   return [walk(x,k) for x in v]
  if isinstance(v,str) and v.startswith('0x'):
   assert k in {'state','pointer','ability','timer','receiver','request'},k
   return ptr(v)
  return v
 return [walk(x) for x in rows]

def selected(z):
 return z.get('event') in EXTRA and (z['event']!='random-seed' or z['caller'] in (0x693734,0x6937dd))
def project(capture_dir):
 r=pathlib.Path(capture_dir);prior.base.r=r
 allrows=[];core=[];scoped=[];locations=[];excluded=[]
 for name in ('observe1','observe2'):
  raw,declared=prior.base.read(name)
  hi=next(i for i,z in enumerate(raw) if z.get('event')=='marker' and ' label=complete' in z['value'])
  rows=raw[:hi+1];base=[z for z in declared if z.get('event') not in EXTRA]
  core.append(prior.norm(base));scoped.append(normalized([z for z in rows if selected(z)]))
  index=0;started=False;loc=[]
  for z in rows:
   if z.get('event')=='marker' and ' label=start' in z['value']:started=True
   if selected(z):loc.append(index)
   elif started and z.get('event') not in EXTRA|{'owned-loading-input','loading-key'}:index+=1
  locations.append(loc)
  excluded.append([z for z in rows if z.get('event')=='random-seed' and not selected(z)])
 old=json.loads(prior.FIXTURE.read_text());assert core[0]==core[1]==old['records']
 assert scoped[0]==scoped[1] and locations[0]==locations[1]
 assert len(scoped[0])==814 and list(map(len,excluded))==[321,321]
 markers=[z['value'] for z in core[0] if z['event']=='marker'];assert markers==old['public'] and len(markers)==1084
 c=[json.loads(l) for l in (r/'control1.jsonl').open()]
 assert c[0]['mode']=='control' and c[0]['owned'] and c[0]['sha256']==old['original_sha256']
 assert c[0]['source_sha256']==json.loads((r/'observe1.jsonl').read_text().splitlines()[0])['source_sha256']
 assert {z['event'] for z in c}<={'metadata','owned-loading-input','loading-key','control-start-file','preload-file'}
 assert any(z.get('event')=='preload-file' and z.get('complete') for z in c)
 assert re.findall(r'PTR [^"\r\n]*',(r/'control1-preload.txt').read_text())==markers
 for n,h in json.loads((r/'frozen-sources.json').read_text()).items():assert hashlib.sha256((r/n).read_bytes()).hexdigest()==h,n
 seeds=[z for z in scoped[0] if z['event']=='streams-reseed-leave'];assert len(seeds)==1 and seeds[0]['seed']==0x77617233 and len(seeds[0]['states'])==45
 return {'schema':1,'original_sha256':old['original_sha256'],'baseline_fixture':str(prior.FIXTURE.relative_to(ROOT)),'baseline_sha256':hashlib.sha256(prior.FIXTURE.read_bytes()).hexdigest(),'baseline_records':14890,'public':markers,'records':scoped[0],'insertion_before_baseline_row':locations[0],'excluded_nonpurpose_seed_counts':[321,321],'limitations':['Full275 baseline is compared literally under its declared pointer/visitstamp policy, not duplicated here.','Only1cc4e0 seed calls directly from693710 at return693734/6937dd enter purpose proof. All321 unrelated seed calls/run remain in raw captures and nonpurpose-seed-rows.json; no repeat claim for them.','Explicit pointer fields and timerrequest words3(clock)/6(receiver) relocated; every other numeric word literal.','Observer owner+54 cached clock and timer request deadline are separate captured values; do not synthesize equality.','Actual265 stored seed and45states are measured. This is native input evidence, not engine replay parity.']}
def mutation_checks(d):
 labels=[]
 for label in ('missing','extra','reorder','seed','random-result','duration','config','deadline','global-order','public'):
  m=copy.deepcopy(d)
  if label=='missing':m['records'].pop()
  elif label=='extra':m['records'].append(m['records'][-1])
  elif label=='reorder':m['records'][0:2]=reversed(m['records'][0:2])
  elif label=='global-order':m['insertion_before_baseline_row'][0]+=1
  elif label=='public':m['public'][0]+=' altered'
  else:
   ev,key={'seed':('streams-reseed-leave','seed'),'random-result':('stream-real','result'),'duration':('attack-timer-request','duration'),'config':('reaction-config','word'),'deadline':('attack-timer-return','request_words')}[label];z=next(z for z in m['records'] if z['event']==ev)
   if label=='deadline':z[key][1]^=1
   else:z[key]^=1
  assert m!=d;labels.append(label)
 return labels
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture-dir',required=True);p.add_argument('--check',action='store_true');a=p.parse_args();d=project(a.capture_dir)
 assert d==json.loads(FIXTURE.read_text()),'missing/extra/reordered/changed record'
 prov=json.loads(FIXTURE.with_suffix('.provenance.json').read_text())
 for n,h in prov['captures'].items():assert hashlib.sha256((pathlib.Path(a.capture_dir)/n).read_bytes()).hexdigest()==h,n
 for n,h in prov['repository_files'].items():assert hashlib.sha256((ROOT/n).read_bytes()).hexdigest()==h,n
 print(json.dumps({'status':'PASS','baseline_records':14890,'scoped_records':814,'public':1084,'negative_controls':len(mutation_checks(d))}))
if __name__=='__main__':main()
