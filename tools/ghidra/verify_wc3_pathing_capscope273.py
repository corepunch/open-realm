#!/usr/bin/env python3
"""Fresh original-code and real engine checks for canonical Captain publication."""
import argparse,copy,gzip,hashlib,json,re,subprocess,sys,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
from capscope273_header import header
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-capscope273-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_capscope273.h'
SOURCES=['tools/frida/research/capscope273_'+x for x in('probe.j','probe.ai','observer.js','make_map.py')]+['tools/ghidra/research/'+x for x in('Capscope273Evidence.java','capscope273_oracle.py','capscope273_header.py')]+['tools/ghidra/verify_wc3_pathing_capscope273.py','tools/frida/research/rank264_capture.py','tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_movement.capscope273_original_target_alias_pending_drop_and_nested_depth','wc3_movement.capscope273_captain_uses_canonical_admission_before_shared_publication','wc3_movement.capscope273_captain_nested_replacement_rejection_and_cold_save']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def markers(text):return re.findall(r'P273 [^"\r\n]*',text)
def normalized(rows):
 out=[];addresses={}
 def record(r):
  if r is None:return None
  address=r['address']
  if address not in addresses:addresses[address]=len(addresses)
  return dict(object=addresses[address],counter=r['counter'],retired=r['retired'])
 for r in rows:
  if r.get('event')not in('scope','target-toggle','publish','published'):continue
  n={k:v for k,v in r.items()if k not in('target','cache','rows')}
  if r['event']=='published':out.append(n);continue
  n['target']=record(r['target']);n['cache']=record(r['cache']);n['rows']=[]
  for member in r['rows']:
   n['rows'].append(dict(index=member['index'],id=member['id'],ready=member['ready']!=0xffffffff,record=record(member['record'])))
  out.append(n)
 return out

def verify_scopes(events):
 publications=[];active=None
 for e in events:
  if e['event']=='publish':
   if active is not None:raise ValueError('overlapping publication')
   active=[e]
  else:
   if active is None:raise ValueError('orphan scope')
   if e['request']!=active[0]['request']:raise ValueError('request changed')
   active.append(e)
   if e['event']=='published':publications.append(active);active=None
 if active is not None:raise ValueError('unfinished publication')
 if len(publications)!=17:raise ValueError('publication coverage')
 targeted=0
 for seq in publications:
  middle=seq[1:-1]
  expected=[('scope','acquire-enter'),('target-toggle','enter'),('target-toggle','leave'),('scope','acquire-leave'),('scope','release-enter'),('target-toggle','enter'),('target-toggle','leave'),('scope','release-leave')]
  if [(x['event'],x['phase'])for x in middle]!=expected:raise ValueError('scope/target chronology')
  if [middle[i]['on']for i in(1,2,5,6)]!=[1,1,0,0]:raise ValueError('toggle ownership')
  initial={r['record']['object']:r['record']['counter']for r in middle[0]['rows']}
  region=middle[0]['cache']or middle[0]['target']
  if region:
   targeted+=1;initial[region['object']]=region['counter']
  for stage,e in enumerate(middle):
   changes={}
   if stage in(3,4):
    for row in middle[0]['rows']:changes[row['record']['object']]=changes.get(row['record']['object'],0)+1
   if region and stage in(2,3,4,5):changes[region['object']]=changes.get(region['object'],0)+1
   for r in [e['target'],e['cache']]+[r['record']for r in e['rows']]:
    if r and r['object']in initial and r['counter']!=initial[r['object']]+changes.get(r['object'],0):raise ValueError('counter lifetime')
 if targeted!=3:raise ValueError('Captain target coverage')
 return dict(publications=17,targeted=3,scope_boundaries=68,target_boundaries=68)

def native_controls(original,control):
 if len(original['rows'])!=24 or len(control['rows'])!=24:raise ValueError('native matrix coverage')
 if {(r['outer'],r['drop'],r['target'])for r in original['rows']}!={(d,b,t)for d in(0,3)for b in(False,True)for t in('none','distinct','alias','invalid','cached','retired')}:raise ValueError('native case identities')
 for a,b in zip(original['rows'],control['rows']):
  if len(a['events'])!=3 or len(b['events'])!=3:raise ValueError('missing native event')
  if {k:v for k,v in a.items()if k!='events'}!={k:v for k,v in b.items()if k!='events'}:raise ValueError('native matrix changed')
  for x,y in zip(a['events'],b['events']):
   if {k:v for k,v in x.items()if k!='trace'}!={k:v for k,v in y.items()if k!='trace'} or y['trace']:raise ValueError('observer perturbs result')

def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('repeats/control missing')
 expected=None;events=None
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta=next(r for r in rows if r.get('event')=='metadata')
  final=rows[-1]
  if final.get('event')!='preload-file' or not final.get('complete')or final.get('markers')!=182:raise ValueError('incomplete capture')
  if meta['sha256']!=SHA or meta['source_sha256']['map']!=spec['map_sha256']or meta['mode']!=('control'if i==2 else'observe'):raise ValueError('capture provenance')
  for r in rows:
   if r.get('type')=='error':raise ValueError('observer error')
  public=markers(c['preload'])
  if len(public)!=182 or public[-1]!='P273 tick=60 label=complete':raise ValueError('public completion')
  if expected is None:expected=public
  elif expected!=public:raise ValueError('changed public behavior')
  if i<2:
   end=next(r for r in rows if r.get('event')=='trace-end')
   if not end['installed']or end['depth']or end['scopes']!=34:raise ValueError('observer closure')
   n=normalized(rows);verify_scopes(n)
   if events is None:events=n
   elif events!=n:raise ValueError('repeat differs')
  elif any(r.get('event')in('scope','target-toggle','publish')for r in rows):raise ValueError('control hooked')
 if events!=spec['events']:raise ValueError('frozen events differ')
 return dict(captures=2,controls=1,public_markers=182,**verify_scopes(events))

def validate(spec):
 if spec['version']!=1 or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES):raise ValueError('contract changed')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source pin '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle pin')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,spec);native_controls(bundle['original'],bundle['control'])
 if header(bundle['original'])!=HEADER.read_text():raise ValueError('literal header differs')
 return bundle

def original_bytes(binary,instructions):
 from foot03_spatial_harness_copy import Emu
 e=Emu(binary)
 if digest(binary)!=SHA:raise ValueError('wrong original')
 for address,raw in instructions.items():
  if bytes(e.uc.mem_read(int(address,16),len(raw)//2)).hex()!=raw:raise ValueError('instruction '+address)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(FIXTURE.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
 import capscope273_oracle as native
 fresh=native.original(a.binary);control=native.original(a.binary,False);native_controls(fresh,control)
 if fresh!=bundle['original']or control!=bundle['control']:raise ValueError('original differs')
 engine=[]
 for tft in(False,True):
  xml=a.report.with_name(a.report.stem+('-tft'if tft else'-classic')+'.xml');log=xml.with_suffix('.log')
  if xml.exists()or log.exists():raise ValueError('fresh engine paths required')
  cmd=[str(a.test_binary.resolve()),'-data',str(a.data.resolve())]+(['-tft']if tft else[])+['+dedicated','1','+test','wc3_movement.capscope273*']
  import os
  with log.open('w')as out:r=subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT,env={**os.environ,'TEST_JUNIT':str(xml.resolve())},timeout=120)
  doc=ET.parse(xml).getroot();cases=list(doc.iter('testcase'));names={c.attrib['name']for c in cases}
  if r.returncode or names!=set(TESTS)or any(list(c)for c in cases):raise ValueError('engine tests failed '+str(log))
  engine.append(dict(tft=tft,tests=len(cases),returncode=r.returncode))
 report=dict(status='verified',passed=True,binary_sha256=SHA,**validate_runtime(bundle,spec),native_cases=24,engine_cases=16,instructions=len(spec['instructions']),engine=engine)
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
