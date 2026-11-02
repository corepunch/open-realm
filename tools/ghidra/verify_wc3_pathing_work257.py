#!/usr/bin/env python3
"""Verify the independent unit exclusion around existing bridge Stop recovery."""
import argparse,gzip,hashlib,json,re,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
import verify_wc3_pathing_work247 as prior
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work257-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/work257_observer.js','tools/ghidra/research/work257_oracle.py',
 'tools/ghidra/research/Work257Evidence.java','tools/frida/research/target222_capture.py',
 'tools/ghidra/fixtures/retail-work247-1.27.json','tools/ghidra/fixtures/retail-work247-1.27.json.gz',
 'tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=prior.TESTS+['wc3_movement.stop257*','wc3_movement.pathing_pause_and_displacement_match_original_and_saved_continuations',
 'pathfinding.fine_scope*','pathfinding.coarse_scope*','pathfinding.captain_distance_excludes_widget_bounds_and_restores_pending_terrain']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def normalized(rows):return [{k:v for k,v in r.items()if k not in('unit','identity')}for r in rows if 'seq'in r]
def runtime(bundle,spec):
 old=json.loads(prior.FIXTURE.read_text());prior.validate_runtime(json.loads(gzip.decompress(prior.BUNDLE.read_bytes())),old)
 if len(bundle['captures'])!=2:raise ValueError('two completed repeats required')
 for c in bundle['captures']:
  rows=c['rows'];m=rows[0];f=rows[-1]
  if m['mode']!='observe'or not m['owned']or m['env']not in('B','C')or m['sha256']!=SHA or not f.get('complete')or f['markers']!=14:raise ValueError('capture incomplete')
  if m['source_sha256']['map']!=old['map_sha256']or m['source_sha256']['work257_observer.js']!=spec['pins'][SOURCES[0]]or m['source_sha256']['target222_capture.py']!=spec['pins'][SOURCES[3]]:raise ValueError('capture provenance differs')
  if hashlib.sha256(c['preload'].encode()).hexdigest()!=f['sha256']or prior.markers(c['preload'])!=old['markers']:raise ValueError('public control differs')
  ends=[r for r in rows if r.get('event')=='trace-end']
  if len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['depth']or any(r.get('type')=='error'for r in rows):raise ValueError('observer incomplete')
  events=normalized(rows)
  if events!=spec['events']:raise ValueError('unit/bridge chronology differs')
  projection=[r.copy()for r in events if not r['event'].startswith(('unit-','widget-'))]
  for i,r in enumerate(projection):r['seq']=i
  if projection!=old['events']:raise ValueError('earlier retail scope expectations changed')
  unit_events=[r for r in events if r['event'].startswith('unit-')]
  if len(unit_events)!=32 or any(r['regions']!=0 for r in unit_events):raise ValueError('ordinary unit coverage differs')
 return dict(captures=2,reused_controls=1,public_markers=28,scope_events=len(spec['events']))
def validate(s):
 if s['version']!=1 or s['task']!='MAP-04.2'or s['game_sha256']!=SHA or set(s['pins'])!=set(SOURCES)or s['engine_tests']!=TESTS:raise ValueError('unit scope contract changed')
 for p,h in s['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source changed '+p)
 if digest(BUNDLE)!=s['bundle_sha256']:raise ValueError('bundle changed')
 b=json.loads(gzip.decompress(BUNDLE.read_bytes()));runtime(b,s)
 if len(b['original']['rows'])!=16 or [{k:v for k,v in r.items()if k!='trace'}for r in b['original']['rows']]!=b['original']['controls']['rows']:raise ValueError('native controls differ')
 return b

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 s=json.loads(FIXTURE.read_text());b=validate(s);original_bytes(a.binary,s['instructions'])
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import work257_oracle as native
 fresh=native.original(a.binary);control=native.original(a.binary,False)
 if fresh!={k:v for k,v in b['original'].items()if k!='controls'}or control!=b['original']['controls']:raise ValueError('fresh native scope differs')
 import verify_wc3_pathing_work242 as runner
 old=runner.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=old
 result=dict(passed=True,task=s['task'],**runtime(b,s),original_cases=16,instructions=len(s['instructions']),engine=engine,
  binary_sha256=SHA,fixture_sha256=digest(FIXTURE),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
