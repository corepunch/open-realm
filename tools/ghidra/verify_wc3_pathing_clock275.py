#!/usr/bin/env python3
"""Fresh original clock/speed evidence and production game/server checks."""
import argparse,gzip,hashlib,json,os,re,subprocess,sys,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-clock275-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_clock275.h'
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCES=['tools/frida/research/clock275_'+x for x in('probe.j','observer.js','make_map.py','capture.py','ui_input.c')]+['tools/ghidra/research/Clock275Evidence.java','tools/ghidra/research/clock275_oracle.py','tools/ghidra/verify_wc3_pathing_clock275.py','tools/frida/research/order0110_ui_input.c','tools/ghidra/research/verify_ORDER-05.1_request_heap.py','tools/ghidra/research/verify_ORDER-03.1_subscriber_dispatch.py','tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_clock.clock275_'+x for x in('public_speed_canonicalization_and_lock','original_setter_clock_words_and_saved_rate','speed_preserves_move_and_saved_continuation')]
SERVER='server_clock.clock275_fixed_steps_rate_pause_hitch_and_fraction'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def header(original):
 text='/* Unmodified retail275 setter/clock-wrapper outputs. Forced inputs are labelled in the oracle. */\n'
 for name,key,n in [('clock275_speeds','speeds',4),('clock275_timeline','timeline',8),('clock275_selectors','selectors',3)]:
  text+='static uint32_t const '+name+'[]['+str(n)+'] = {\n'+''.join(' {'+','.join('0x%08xu'%x for x in row)+'},\n'for row in original[key])+'};\n'
 return text
def check_events(rows):
 markers=[r for r in rows if r.get('event')=='marker'];hosts=[r for r in rows if r.get('event')=='host']
 if len(markers)!=104 or not markers[-1]['value'].startswith('C275 tick=100 label=complete'):raise ValueError('marker coverage')
 speeds=[(r['input'],r['canonical'])for r in rows if r.get('event')=='speed']
 if speeds!=[(1,1),(2,2),(3,2),(4,2)]:raise ValueError('speed conversion')
 selectors=markers[-1]['selectors']
 if min(selectors)<=0:raise ValueError('both selectors required')
 for r in markers:
  c=r['clocks']
  if c['primary']['span']!=0x43960000 or c['presentation']['span']!=0x43960000:raise ValueError('configured spans')
  if r['inputs'] not in ({},{str(0x3ba3d70a):r['primary']}):raise ValueError('primary step rescaled')
  if r['primary'] and r['primary']!=int(re.search(r'tick=(\d+)',r['value'])[1])*20+1:raise ValueError('primary event timeline')
 if {r['rate']for r in hosts if r['after']>r['before']}!={0,1,2}:raise ValueError('active host rate coverage')
 for r in hosts:
  n=r['after']-r['before']
  if n<0 or n%(r['rate']+3):raise ValueError('host batch quantum')
 pauses=[r for r in rows if r.get('event')=='pause']
 if [r['pause']for r in pauses]!=[1,0,1,0]:raise ValueError('public script/menu pause')
 begin,end=pauses[-2:]
 beats=[r for r in rows if r.get('event')=='heartbeat'and begin['wall']+100<r['wall']<end['wall']-100]
 if end['wall']-begin['wall']<1800 or len(beats)<10 or any(r['clocks']!=beats[0]['clocks']for r in beats):raise ValueError('settled pause changes clocks')
 return dict(public_markers=104,primary_events=markers[-1]['primary'],both_selectors=True,host_quanta_verified=True,pause_frozen=True)
def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('repeat/control missing')
 public=None
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta=rows[0]
  if meta.get('sha256')!=SHA or meta.get('task')!='SCHED-01.1' or meta.get('mode')!=('observe'if i<2 else'control'):raise ValueError('capture provenance')
  if not rows[-1].get('complete')or rows[-1].get('markers')!=104 or any(r.get('event')=='trace-failed'for r in rows):raise ValueError('capture incomplete')
  if meta['source_sha256']['map']!=spec['map_sha256']:raise ValueError('wrong public map')
  markers=re.findall(r'call Preload\( "(C275 [^"\r\n]*)" \)',c['preload'])
  if len(markers)!=104:raise ValueError('public marker count')
  if public is None:public=markers
  elif markers!=public:raise ValueError('observer perturbed motion')
  if i<2:check_events(rows)
  elif any(r.get('event')in('marker','host','pause','heartbeat')for r in rows):raise ValueError('hooked control')
 return dict(captures=2,controls=1,**check_events(bundle['captures'][0]['rows']))
def validate(spec):
 if spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES):raise ValueError('contract')
 for name,h in spec['pins'].items():
  if digest(ROOT/name)!=h:raise ValueError('source pin '+name)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle pin')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,spec)
 if HEADER.read_text()!=header(bundle['original']):raise ValueError('literal original header')
 return bundle
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(FIXTURE.read_text());bundle=validate(spec)
 if digest(a.binary)!=SHA:raise ValueError('wrong original')
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
 import clock275_oracle as native
 from foot03_spatial_harness_copy import Emu
 e=Emu(a.binary)
 for address,raw in spec['instructions'].items():
  if bytes(e.uc.mem_read(int(address,16),len(raw)//2)).hex()!=raw:raise ValueError('instruction '+address)
 if native.execute(a.binary)!=bundle['original']:raise ValueError('original changed')
 # Replay the live primary clock from its source events, using original addition.
 import importlib.util
 m=importlib.util.spec_from_file_location('clock275_requests',ROOT/'tools/ghidra/research/verify_ORDER-05.1_request_heap.py');requests=importlib.util.module_from_spec(m);m.loader.exec_module(requests)
 scalar=requests.Scalar(a.binary.read_bytes());times=[0]
 for _ in range(2001):times.append(scalar.add(times[-1],0x3ba3d70a))
 for c in bundle['captures'][:2]:
  for r in c['rows']:
   if r.get('event')=='marker'and r['clocks']['primary']['time']!=times[max(0,r['primary']-1)]:raise ValueError('live primary scalar word')
 engine=[]
 for tft in(False,True):
  for pattern,names in [('wc3_clock.clock275*',TESTS),('server_clock.clock275*',[SERVER])]:
   stem=str(a.report.with_suffix(''))+('-tft'if tft else'-classic')+('-server'if len(names)==1 else'-game');xml=Path(stem+'.xml');log=Path(stem+'.log')
   if xml.exists()or log.exists():raise ValueError('fresh validation paths required')
   command=[str(a.test_binary.resolve()),'-data',str(a.data.resolve()),*(['-tft']if tft else[]),'+dedicated','1','+test',pattern]
   with log.open('w')as out:r=subprocess.run(command,stdout=out,stderr=subprocess.STDOUT,env={**os.environ,'TEST_JUNIT':str(xml)},timeout=120)
   cases=list(ET.parse(xml).getroot().iter('testcase'))
   if r.returncode or {c.attrib['name']for c in cases}!=set(names)or any(list(c)for c in cases):raise ValueError('engine '+str(log))
   engine.append(dict(tft=tft,tests=len(cases),assertions=int(ET.parse(xml).getroot().get('assertions')),returncode=r.returncode))
 report=dict(status='verified',passed=True,binary_sha256=SHA,**validate_runtime(bundle,spec),original_speeds=1542,original_timeline=32,original_selectors=5,instructions=len(spec['instructions']),engine=engine)
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
