#!/usr/bin/env python3
"""Verify ordinary target replacement against repeated original native decisions.

The new capture contract is independent of existing expected retail fixtures.
No TownAI, artillery-area, ability override or present-perimeter parity is claimed.
"""
from retail_capture_source import matches_capture_source
import argparse,copy,gzip,hashlib,json,re,sys
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-ranking264-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/rank264_'+s for s in('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/queue263_ui_input.c','tools/frida/research/order0110_ui_input.c',
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/ghidra/research/Ranking264Evidence.java','tools/ghidra/research/rank264_distance_oracle.py',
 'tools/ghidra/research/foot03_spatial_harness_copy.py','games/warcraft-3/game/tests/fixtures/retail_ranking264_distance.h']
TESTS=['wc3_order_lifecycle.*','wc3_order_reentry.*','wc3_interrupt.*','wc3_movement.group_move_attack*',
 'wc3_movement.target251*','wc3_movement.target220*','wc3_save.rejects_prior_save_versions']
VARIANTS=('near','sticky','tie','farther','threat','worker','disarmed','immobile')
# Frozen values read from original EDX:EAX, followed by original49e3a0 EAX.
DECISIONS={
 'near':([0x64000000,0x0be00000],[0x60000000,0x0be00000],1),
 'sticky':([0x64000000,0x0be00000],[0x65000000,0x0be00000],0),
 'tie':([0x60000000,0x0be00000],[0x60000000,0x0be00000],1),
 'farther':([0x60000000,0x0be00000],[0x60000000,0x0be00000],0),
 'threat':([0x64000000,0x0be00000],[0x65400000,0x0be00000],0),
 'worker':([0x64000000,0x0be00000],[0x60000000,0x0be00000],1),
 'disarmed':([0x64000000,0x0ba00000],[0x60000000,0x0be00000],0),
 'immobile':([0x44000000,0x0be00000],[0x60000000,0x0be00000],0)}
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(s):return re.findall(r'call Preload\( "(R264 [^"\r\n]*)" \)',s)
def projection(rows):
 result=[]
 for row in rows:
  if 'seq'not in row or row['event']=='module':continue
  row=copy.deepcopy(row);row.pop('seq')
  # Process addresses are retained in the raw bundle but are not identities.
  row.pop('ability',None);row.pop('receiver',None)
  for key in('actor','candidate'):
   unit=row.get(key)
   if not isinstance(unit,dict):continue
   unit['move_present']=unit.pop('move')!='0x0'
   if unit['attack']:unit['attack'].pop('pointer')
  result.append(row)
 return result

def runtime(bundle,spec):
 if set(bundle)!=set(VARIANTS):raise ValueError('missing ranking domain')
 compares=0
 for variant,cases in bundle.items():
  if len(cases)!=3 or [c['rows'][0]['mode']for c in cases]!=['observe','observe','control']:raise ValueError('repeat/control missing')
  for case in cases:
   rows=case['rows'];meta,end=rows[0],rows[-1];public=markers(case['preload'])
   if(meta.get('task')!='GROUP-03.2'or meta.get('sha256')!=SHA or not meta.get('owned')or meta.get('env')not in('B','C')or
      meta['source_sha256']!={**spec['capture_sources'],'map':spec['maps'][variant]}or
      any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('ranking capture provenance/failure')
   if end!={'event':'preload-file','sha256':hashlib.sha256(case['preload'].encode()).hexdigest(),'markers':5,'complete':True}or public!=spec['public'][variant]:raise ValueError('public ranking stream differs')
   if meta['mode']=='control':
    if any('seq'in r or r['event']=='trace-end'for r in rows):raise ValueError('instrumented control')
    continue
   foot=[r for r in rows if r['event']=='trace-end']
   if len(foot)!=1 or not foot[0].get('readOnly')or not foot[0].get('installed')or foot[0]['depth']!=0:raise ValueError('unfinished observer')
   if foot[0]['counts']!=dict(Counter(r['event']for r in rows if 'seq'in r)):raise ValueError('observer counts differ')
   if [r['value']for r in rows if r['event']=='marker']!=public or projection(rows)!=spec['events'][variant]:raise ValueError('original event stream differs')
   modules=[r for r in rows if r['event']=='module']
   if len(modules)!=1 or modules[0]['minimumRange']!=0x42000000:raise ValueError('native minimum range differs')
   ranks=[r['key']for r in rows if r['event']=='rank-end'];results=[r['accepted']for r in rows if r['event']=='compare-end']
   incoming,retained,decision=DECISIONS[variant]
   if ranks[:2]!=[incoming,retained]or results[0]!=decision:raise ValueError('native ranking decision differs')
   calls=[r for r in rows if r['event']=='rank']
   if calls[0]['bound']!=[0,0]or calls[1]['bound']!=incoming:raise ValueError('native pruning bound differs')
   if any(r['actor']['flags']&4 for r in rows if r['event']=='notify'and r['actor']['identity']==calls[0]['actor']):raise ValueError('TownAI substituted for ordinary observer')
   if not any(r['event']=='exempt'and r['seq']<calls[0]['seq']for r in rows):raise ValueError('ranking preceded exemption')
   ranges=[r for r in rows if r['event']=='range'and r['caller']=='49da13']
   if any(r['mode']!=0 for r in ranges):raise ValueError('predicted reach substituted')
   distances=[r for r in rows if r['event']=='distance']
   if variant in('tie','farther'):
    if len(distances)!=2 or [r['caller']for r in distances]!=['49e4bd','49e4df']:raise ValueError('tie distance missing')
    if (distances[1]['word']<distances[0]['word'])!=bool(decision):raise ValueError('distance ordering differs')
   elif distances:raise ValueError('unequal priorities used distance')
   compares+=len(results)
 return dict(captures=24,public_markers=120,native_comparisons=compares,domains=8)

def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-03.2'or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or
    spec['engine_tests']!=TESTS or set(spec['maps'])!=set(VARIANTS)or len(spec['instructions'])!=spec['instruction_count']):raise ValueError('ranking contract differs')
 for p,h in spec['pins'].items():
  if not matches_capture_source(ROOT,p,h):raise ValueError('source changed '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('ranking bundle changed')
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
 import rank264_distance_oracle as distance
 if len(spec['distance_rows'])!=16 or (ROOT/SOURCES[-1]).read_text()!=distance.header(spec['distance_rows']):raise ValueError('original distance/header differs')
 return spec

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 s=validate(json.loads(FIXTURE.read_text()));b=json.loads(gzip.decompress(BUNDLE.read_bytes()));live=runtime(b,s);original_bytes(a.binary,s['instructions'])
 import rank264_distance_oracle as distance
 if distance.original(a.binary)!=s['distance_rows']:raise ValueError('fresh original distance differs')
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,task=s['task'],**live,instructions=len(s['instructions']),original_distance_cases=16,engine=engine,limits=s['limits'],
  binary_sha256=SHA,fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),test_binary_sha256=digest(a.test_binary),
  game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'))
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
