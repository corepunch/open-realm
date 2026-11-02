#!/usr/bin/env python3
"""Fresh original and engine proofs for TARGET-04.1 refresh thresholds/producers."""
import argparse,gzip,hashlib,json,re,subprocess,sys,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
from refresh274_header import header
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-refresh274-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_refresh274.h'
SOURCES=['tools/frida/research/refresh274_'+x for x in('probe.j','observer.js','make_map.py')]+['tools/ghidra/research/'+x for x in('Refresh274Evidence.java','refresh274_oracle.py','refresh274_header.py')]+['tools/ghidra/verify_wc3_pathing_refresh274.py','tools/frida/research/rank264_capture.py','tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_movement.refresh274_'+x for x in('original_thresholds_and_captain_extra_policy','actual_owner_cadence_and_saved_captain_delay','original_prediction_order_and_fine_only_reload')]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def normalized(rows):
 return [{k:v for k,v in r.items()if k!='result'or r['event']!='handoff'}for r in rows if r.get('event')in('owner','sample','refresh','request','extra-producer','handoff')]
def check_events(rows):
 refresh=[r for r in rows if r['event']=='refresh'];reloads=[r for r in refresh if r['before']['cd']==-1]
 if len(refresh)!=5473 or len(reloads)!=218:raise ValueError('refresh coverage')
 for r in refresh:
  before=r['before']['cd'];want=min(132,max(16,r['candidate']))+ (165 if r['flags']&0x400 else 0)if before==-1 else before-int(before!=0)
  if r['cd']!=want:raise ValueError('native countdown/reload')
 previous={};intervals=[]
 for r in reloads:
  key=tuple(r['id'])
  if key in previous:
   old=previous[key];period=r['c']-old['c'];intervals.append(period)
   if period!=old['cd']+1:raise ValueError('owner sample cadence')
  previous[key]=r
 if len(intervals)!=203 or intervals.count(17)!=195 or intervals.count(182)!=8:raise ValueError('ordinary/extra cadence coverage')
 extra=[r for r in reloads if r['flags']&0x400]
 if len(extra)!=10 or any(r['flags']!=0x21c01 or r['cd']!=181 for r in extra):raise ValueError('shared Captain policy')
 groups={tuple(r['id'])for r in extra}
 if len(groups)!=2:raise ValueError('physical target cohort count')
 for key in groups:
  times=[r['c']for r in extra if tuple(r['id'])==key]
  if times!=[1425,1607,1789,1971,2153]:raise ValueError('actual extra refresh counters')
  owners=[o for o in rows if o['event']=='owner'and tuple(o['id'])==key]
  if len(owners)!=834 or [o['c']for o in owners]!=list(range(1425,2259))or owners[-1]['cd']!=77:raise ValueError('Captain lifetime')
 producers=[r for r in rows if r['event']=='extra-producer']
 if len(producers)!=8 or any(r['caller']!=0x9d1136 or r['enable']!=1 for r in producers):raise ValueError('producer caller')
 if sum(r['flags']==0x800 for r in producers)!=2 or sum(r['flags']==0x900 for r in producers)!=6:raise ValueError('point/target policy')
 handoffs=[r for r in rows if r['event']=='handoff']
 if sum(r['flags']==0x1c01 for r in handoffs)!=24 or sum(r['flags']==0xd00 for r in handoffs)!=72:raise ValueError('canonical flag copy')
 return dict(refreshes=5473,reloads=218,ordinary_intervals=195,extra_intervals=8,extra_reloads=10,captain_owner_visits=834,live_extra_next_sample_observed=True)
def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('missing repeat/control')
 public=None;events=None
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta=rows[0];last=rows[-1]
  if meta.get('sha256')!=SHA or meta['source_sha256']['map']!=spec['map_sha256']or meta['mode']!=('control'if i==2 else'observe'):raise ValueError('provenance')
  if last.get('event')!='preload-file'or not last['complete']or last['markers']!=402 or any(r.get('type')=='error'for r in rows):raise ValueError('incomplete capture')
  markers=re.findall(r'R274 [^"\r\n]*',c['preload'])
  if len(markers)!=402 or markers[-1]!='R274 tick=400 label=complete':raise ValueError('public completion')
  if public is None:public=markers
  elif public!=markers:raise ValueError('observer perturbed public motion')
  n=normalized(rows)
  if i<2:
   check_events(n)
   if events is None:events=n
   elif events!=n:raise ValueError('native repeat differs')
  elif n:raise ValueError('hooked control')
 if hashlib.sha256(json.dumps(events,sort_keys=True,separators=(',',':')).encode()).hexdigest()!=spec['events_sha256']:raise ValueError('frozen events differ')
 return dict(captures=2,controls=1,public_markers=402,**check_events(events))
def validate(spec):
 if spec['version']!=1 or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES):raise ValueError('contract')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source pin '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle pin')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,spec)
 if header(bundle['original'])!=HEADER.read_text():raise ValueError('literal header differs')
 if [len(bundle['original'][k])for k in('rows','prediction','countdown')]!=[4418,180,298]:raise ValueError('original coverage')
 return bundle
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(FIXTURE.read_text());bundle=validate(spec)
 from foot03_spatial_harness_copy import Emu
 if digest(a.binary)!=SHA:raise ValueError('wrong original')
 e=Emu(a.binary)
 for address,raw in spec['instructions'].items():
  if bytes(e.uc.mem_read(int(address,16),len(raw)//2)).hex()!=raw:raise ValueError('instruction '+address)
 import refresh274_oracle as native
 if native.original(a.binary)!=bundle['original']:raise ValueError('original differs')
 engine=[]
 for tft in(False,True):
  xml=a.report.with_name(a.report.stem+('-tft'if tft else'-classic')+'.xml');log=xml.with_suffix('.log')
  if xml.exists()or log.exists():raise ValueError('fresh engine outputs required')
  cmd=[str(a.test_binary.resolve()),'-data',str(a.data.resolve())]+(['-tft']if tft else[])+['+dedicated','1','+test','wc3_movement.refresh274*']
  import os
  with log.open('w')as out:r=subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT,env={**os.environ,'TEST_JUNIT':str(xml.resolve())},timeout=120)
  cases=list(ET.parse(xml).getroot().iter('testcase'))
  if r.returncode or {c.attrib['name']for c in cases}!=set(TESTS)or any(list(c)for c in cases):raise ValueError('engine '+str(log))
  engine.append(dict(tft=tft,tests=len(cases),returncode=r.returncode))
 report=dict(status='verified',passed=True,binary_sha256=SHA,**validate_runtime(bundle,spec),original_thresholds=4418,original_predictions=180,original_countdowns=298,instructions=len(spec['instructions']),engine=engine)
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
