#!/usr/bin/env python3
"""Verify current-form Stop admission and the original structure-support predicate."""
from retail_capture_source import matches_capture_source
import argparse,gzip,hashlib,json,re,sys
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
import verify_wc3_pathing_work247 as prior
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work258-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/work258_'+s for s in('observer.js','make_map.py','probe.j')]+[
 'tools/ghidra/research/Work258Evidence.java','tools/ghidra/research/work258_oracle.py',
 'tools/frida/research/target222_capture.py','tools/frida/research/group032_make_map.py',
 'tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/fixtures/retail-work247-1.27.json',
 'tools/ghidra/fixtures/retail-work247-1.27.json.gz','tools/ghidra/research/work247_oracle.py',
 'tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_movement.stop258*','wc3_movement.stop257*','wc3_movement.stop247*',
 'wc3_ancient_root.*','wc3_movement.recovery183*','wc3_movement.public_stop*',
 'wc3_order_lifecycle.*','wc3_interrupt.*','wc3_movement.target256*',
 'wc3_movement.primary_clock_public_move_save_pause_and_stop',
 'wc3_movement.pathing_pause_and_displacement_match_original_and_saved_continuations',
 'wc3_save.fine_spatial*','wc3_save.rejects_prior_save_versions']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(W258 [^"\r\n]*)" \)',text)
def normalized(rows):return [{k:v for k,v in r.items()if k not in('unit','identity')}for r in rows if 'seq'in r]
def runtime(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('two observations and a control required')
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];m=rows[0];f=rows[-1];events=normalized(rows)
  if(m['mode']!=('control'if i==2 else'observe')or not m['owned']or m['env']not in('B','C')or m['sha256']!=SHA or
     not f.get('complete')or f['markers']!=15 or markers(c['preload'])!=spec['markers']or
     hashlib.sha256(c['preload'].encode()).hexdigest()!=f['sha256']or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):
   raise ValueError('capture incomplete or public control differs')
  if m['source_sha256']['map']!=spec['map_sha256']:raise ValueError('map differs')
  for p in(SOURCES[0],SOURCES[5]):
   if m['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('capture provenance differs')
  ends=[r for r in rows if r.get('event')=='trace-end']
  if i==2:
   if events or ends:raise ValueError('control instrumented')
   continue
  if len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['depth']or ends[0]['counts']!=dict(Counter(r['event']for r in events)):raise ValueError('observer incomplete')
  if [r['value']for r in rows if r.get('event')=='marker']!=spec['markers']or events!=spec['events']:raise ValueError('public Stop chronology differs')
  entries=[r for r in events if r['event']=='unit-stop-enter']
  if Counter(r['scene']for r in entries)!={0:2,3:2}or [(r['flags'],r['regions'])for r in entries]!=[(513,0)]*2+[(103041,3)]*2:raise ValueError('current form gate differs')
  if [r['flags']for r in events if r['event']=='widget-toggle-exit']!=[[0x10000001]*3,[0x10000000]*3]*2:raise ValueError('mobile widget exclusion differs')
  if any(not r['callback']for r in events if r['event']=='stop-enter')or [r['counter']for r in events if r['event']=='stop-mover-enter']!=[2]*4:raise ValueError('nonstructure bridge depth differs')
 meta=bundle['map_metadata']
 if meta['map_sha256']!=spec['map_sha256']or meta['fine_origin']!=[0,0]or meta['shadow_size']!=4096:raise ValueError('map geometry differs')
 for key,path in [('target_builder_sha256',SOURCES[1]),('probe_sha256',SOURCES[2]),('builder_sha256',SOURCES[6]),('shared_builder_sha256',SOURCES[7])]:
  if meta[key]!=spec['pins'][path]:raise ValueError('map source differs')
 return dict(captures=2,controls=1,public_markers=45,scope_events=len(spec['events']))
def validate(s):
 if s['version']!=1 or s['task']!='MAP-04.2'or s['game_sha256']!=SHA or set(s['pins'])!=set(SOURCES)or s['engine_tests']!=TESTS:raise ValueError('structure Stop contract changed')
 for p,h in s['pins'].items():
  if not matches_capture_source(ROOT,p,h):raise ValueError('source changed '+p)
 if digest(BUNDLE)!=s['bundle_sha256']:raise ValueError('bundle changed')
 b=json.loads(gzip.decompress(BUNDLE.read_bytes()));runtime(b,s)
 old=prior.validate(json.loads(prior.FIXTURE.read_text()))
 null=[r for r in old['original']['rows']if r['kind']=='no_callback']
 if len(null)!=8 or any(r['callback_calls']or any(r['path'])for r in null):raise ValueError('earlier null callback contract differs')
 if len(b['original'])!=16:raise ValueError('missing original predicate decisions')
 return b

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 s=json.loads(FIXTURE.read_text());b=validate(s);original_bytes(a.binary,s['instructions'])
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import work258_oracle as native;import work247_oracle as previous
 if native.original(a.binary)!=b['original']:raise ValueError('fresh original predicate differs')
 old=json.loads(gzip.decompress(prior.BUNDLE.read_bytes()))['original'];fresh=previous.original(a.binary);control=previous.original(a.binary,False)
 if fresh!={k:v for k,v in old.items()if k!='controls'}or control['rows']!=old['controls']:raise ValueError('fresh original bridge differs')
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,task=s['task'],**runtime(b,s),original_cases=16,reused_bridge_cases=32,instructions=len(s['instructions']),engine=engine,
  binary_sha256=SHA,fixture_sha256=digest(FIXTURE),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
