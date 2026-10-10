#!/usr/bin/env python3
"""Verify original negative-origin fine target sampling and engine retention."""
import argparse,gzip,hashlib,json,re,struct
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work246-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_target246.h'
SOURCES=['tools/frida/research/work246_'+s for s in('observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/target222_capture.py','tools/frida/research/group032_make_map.py',
 'tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/Work246Evidence.java']
TESTS=['wc3_movement.target246*','wc3_movement.target166*','wc3_movement.target218*',
 'wc3_movement.target220*','wc3_movement.target221*','wc3_movement.target222*',
 'wc3_movement.target223*','wc3_movement.target224*','wc3_movement.policy245*',
 'wc3_movement.persistent_group_fog_retains_sample_until_refresh','wc3_movement.disabled234*',
 'wc3_movement.selected*','wc3_movement.periodic_public*','wc3_movement.cohort232*',
 'wc3_movement.source233*','wc3_movement.partition235*','wc3_movement.queued236*',
 'wc3_movement.formation_refresh*','wc3_movement.delayed164*',
 'wc3_movement.public_captain_go_home*','wc3_movement.public_group_move*',
 'wc3_save.rejects_prior_save_versions','wc3_save.rejects_layout_mismatch_before_selecting_map']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(W246 [^"\r\n]*)" \)',preload)
def sequence(rows):return[{k:v for k,v in r.items()if k not in('seq','g','path')}for r in rows if r.get('event')in('sample','route','setdest','marker')]
def samples(rows):
 out=[]
 for r in rows:
  if r.get('event')=='sample'and r['target']!=[0,0]and r['cd']==0 and r['dest']not in out:out.append(r['dest'])
 return out
def header(rows):
 return '\n'.join(['#ifndef RETAIL_TARGET246_H','#define RETAIL_TARGET246_H',
  '/* Exact original public Smart refresh samples at world origin -8192; never engine-generated. */',
  'static uint32_t const target246_samples[][2]={']+[
  '{'+','.join(f'0x{v:08x}u'for v in pair)+'},'for pair in samples(rows)]+['};','#endif'])+'\n'
def roundtrip(v):
 f=lambda b:struct.unpack('<f',struct.pack('<I',b))[0]
 b=lambda f:struct.unpack('<I',struct.pack('<f',f))[0]
 return b((f(b(f(v)*32-8192))+8192)/32)
def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=3:raise ValueError('two repeats and a control required')
 for i,c in enumerate(captures):
  rows,preload=c['rows'],c['preload'];meta,footer=rows[0],rows[-1]
  if(meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or
     meta['source_sha256']['map']!=spec['map_sha256']or meta['mode']!=('control'if i==2 else'observe')or
     not footer.get('complete')or footer['markers']!=74 or markers(preload)!=spec['markers']or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('capture incomplete or changed')
  for p in(SOURCES[0],SOURCES[3]):
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('observer provenance differs')
  if i==2:
   if any('seq'in r or r.get('event')=='trace-end'for r in rows):raise ValueError('control was instrumented')
   continue
  ends=[r for r in rows if r.get('event')=='trace-end']
  if(len(ends)!=1 or not ends[0].get('installed')or not ends[0].get('readOnly')or
     ends[0]['counts']!=dict(Counter(r['event']for r in rows if 'seq'in r))or
     sequence(rows)!=spec['sequence']or [r['value']for r in rows if r.get('event')=='marker']!=spec['markers']+[spec['trailing_marker']]):raise ValueError('repeat differs')
  pending={};loss=0;routes=0;retained=0
  for r in rows:
   if r.get('event')=='sample':
    pending[(r['g'],r['c'])]=r['dest']
    if r['target']!=[0,0]and r['cd']==0:loss+=any(roundtrip(v)!=v for v in r['dest'])
   elif r.get('event')=='route':
    if pending.pop((r['g'],r['c']),None)!=r['input']or r['result']!=1:raise ValueError('sample/route input words differ')
    if r['dest']!=r['input']:retained+=1
    routes+=1
  if loss!=13 or routes!=450 or retained!=2 or pending:raise ValueError('target precision witnesses changed')
 if header(captures[0]['rows'])!=HEADER.read_text():raise ValueError('retail sample fixture changed')
 build=bundle['map_metadata']
 if build['map_sha256']!=spec['map_sha256']or build['fine_origin']!=[-8192,-8192]or build['shadow_size']!=4096:raise ValueError('map geometry differs')
 for key,path in [('target_builder_sha256',SOURCES[1]),('probe_sha256',SOURCES[2]),('builder_sha256',SOURCES[4]),('shared_builder_sha256',SOURCES[5])]:
  if build[key]!=spec['pins'][path]:raise ValueError('map source provenance differs')
 return dict(captures=2,controls=1,public_markers=222,route_samples=900,loss_witnesses=26,fine_samples=len(samples(captures[0]['rows'])))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(FIXTURE.read_text())
 if spec['version']!=1 or spec['task']!='TARGET-02.1' or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS:raise ValueError('contract changed')
 for path,sha in spec['pins'].items():
  if digest(ROOT/path)!=sha:raise ValueError('changed source '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));live=validate_runtime(bundle,spec)
 original_bytes(a.binary,spec['instructions'])
 a.report.parent.mkdir(parents=True,exist_ok=True)
 import verify_wc3_pathing_work242 as prior
 old=prior.TESTS
 try:prior.TESTS=TESTS;engine=prior.run_engine(a.test_binary,a.data,a.report)
 finally:prior.TESTS=old
 result=dict(passed=True,task=spec['task'],**live,instructions=len(spec['instructions']),engine=engine,fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
