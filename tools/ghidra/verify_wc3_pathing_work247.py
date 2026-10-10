#!/usr/bin/env python3
"""Verify nested bridge Stop/recovery scopes without changing older expectations."""
import argparse,gzip,hashlib,json,re,struct
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work247-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_stop247.h'
SOURCES=['tools/frida/research/work247_'+s for s in('observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/target222_capture.py','tools/frida/research/group032_make_map.py',
 'tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/Work247Evidence.java',
 'tools/ghidra/research/work247_oracle.py','tools/ghidra/research/foot03_rig.py',
 'tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_movement.stop247*','wc3_movement.recovery183*','wc3_movement.public_stop*',
 'wc3_movement.primary_clock_public_move_save_pause_and_stop',
 'wc3_movement.public_speed_turn_stop_and_boundary_restart*',
 'wc3_movement.selected*','wc3_movement.target221*','wc3_movement.public_captain_go_home*',
 'wc3_order_lifecycle.*','wc3_interrupt.*',
 'wc3_save.rejects_prior_save_versions','wc3_save.rejects_layout_mismatch_before_selecting_map']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(W247 [^"\r\n]*)" \)',text)
def header(original):
 out=['#ifndef RETAIL_STOP247_H','#define RETAIL_STOP247_H',
  '/* Original05ca50 ->171340 ->170080; fixture terrain support level0. */',
  'static struct {unsigned kind,cls,outer;uint32_t pose[2];} const stop247_rows[]={']
 for r in original['rows']:
  out.append('{%d,%d,%d,{0x%08xu,0x%08xu}},'%(['clear','admitted','exhausted','no_callback'].index(r['kind']),r['cls'],r['outer'],*r['pose'][:2]))
 return '\n'.join(out+['};','#endif',''])
def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('two repeats and a control required')
 for i,c in enumerate(bundle['captures']):
  rows,preload=c['rows'],c['preload'];meta,footer=rows[0],rows[-1]
  if(meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or
    meta['source_sha256']['map']!=spec['map_sha256']or meta['mode']!=('control'if i==2 else'observe')or
    not footer.get('complete')or footer['markers']!=14 or markers(preload)!=spec['markers']or
    hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or
    any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('capture incomplete or changed')
  for p in(SOURCES[0],SOURCES[3]):
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('observer provenance differs')
  if i==2:
   if any('seq'in r or r.get('event')=='trace-end'for r in rows):raise ValueError('control instrumented')
   continue
  events=[r for r in rows if 'seq'in r];ends=[r for r in rows if r.get('event')=='trace-end']
  if(events!=spec['events']or len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['depth']or
    ends[0]['counts']!=dict(Counter(r['event']for r in events))or
    [r['value']for r in rows if r.get('event')=='marker']!=spec['markers']):raise ValueError('scope sequence differs')
  if Counter(r['event']for r in events)!=dict(spec['counts']):raise ValueError('scope counts differ')
 meta=bundle['map_metadata']
 if meta['map_sha256']!=spec['map_sha256']or meta['fine_origin']!=[0,0]or meta['shadow_size']!=4096:raise ValueError('map geometry differs')
 for key,path in [('target_builder_sha256',SOURCES[1]),('probe_sha256',SOURCES[2]),('builder_sha256',SOURCES[4]),('shared_builder_sha256',SOURCES[5])]:
  if meta[key]!=spec['pins'][path]:raise ValueError('map provenance differs')
 return dict(captures=2,controls=1,public_markers=42,bridge_scopes=16,placement_searches=6,commits=2)
def validate(spec):
 if(spec['version']!=1 or spec['task']!='MAP-04.2'or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS):raise ValueError('contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('changed source '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle changed')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,spec)
 if header(bundle['original'])!=HEADER.read_text():raise ValueError('original fixture changed')
 if [{k:v for k,v in r.items()if k!='trace'}for r in bundle['original']['rows']]!=bundle['original']['controls']:raise ValueError('original controls differ')
 return bundle
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(FIXTURE.read_text());bundle=validate(spec);live=validate_runtime(bundle,spec);original_bytes(a.binary,spec['instructions'])
 import sys
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
 import work247_oracle as native
 original=native.original(a.binary);control=native.original(a.binary,False)
 if original!={k:v for k,v in bundle['original'].items()if k!='controls'}or control['rows']!=bundle['original']['controls']:raise ValueError('fresh original differs')
 import verify_wc3_pathing_work242 as prior
 old=prior.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:prior.TESTS=TESTS;engine=prior.run_engine(a.test_binary,a.data,a.report)
 finally:prior.TESTS=old
 result=dict(passed=True,task=spec['task'],**live,original_cases=len(original['rows']),original_controls=len(control['rows']),instructions=len(spec['instructions']),engine=engine,fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
