#!/usr/bin/env python3
"""Verify the original neutral guard timer producer and unchanged pursuit rows."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_fog252 import fog_rows,target_rows,words_digest
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-guard254-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER='games/warcraft-3/game/tests/fixtures/retail_reacquire252.h'
SOURCES=['tools/frida/research/target254_make_map.py','tools/frida/research/target254_restart_observer.js',
 'tools/frida/research/target03_capture.py','tools/frida/research/target021_observer.js',
 'tools/frida/research/target03_vis_observer.js','tools/frida/research/target03r_probe.j',
 'tools/ghidra/research/Work254Evidence.java',HEADER]
TESTS=['wc3_movement.target254*','wc3_movement.target252*','wc3_order_lifecycle.guard254*',
 'wc3_save.rejects_prior_save_versions','wc3_movement.public_timer*','wc3_ability_dispatch.owner254*','wc3_ability_dispatch.engine_stand*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def timer_rows(rows):
 return [[r['c'],r['code'],r['delay'],r['periodic']]for r in rows if r.get('event')=='guard-arm254']
def verify_runtime(bundle,spec):
 if len(bundle['captures'])!=2 or len(bundle['controls'])!=1:raise ValueError('missing repeated/control capture')
 markers=None
 for n,c in enumerate(bundle['captures']+bundle['controls']):
  rows=c['rows'];m=rows[0];f=rows[-1];p=c['preload']
  public=re.findall(r'call Preload\( "(T3R [^"\r\n]*)" \)',p)
  if(m.get('event')!='metadata' or m.get('mode')!=('observe'if n<2 else'control')or not m.get('owned')or m['sha256']!=SHA or
   m.get('remote')not in('127.0.0.1:27049','127.0.0.1:27050')or m.get('display')not in(':98',':99')or
   m['source_sha256']['map']!=spec['map_sha256']or f.get('event')!='preload-file'or not f.get('complete')or f['markers']!=215 or
   len(public)!=215 or hashlib.sha256(p.encode()).hexdigest()!=f['sha256']or
   any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('incomplete/unowned capture')
  if markers is not None and public!=markers:raise ValueError('observer/control gameplay changed')
  markers=public
  if n==2:
   if any(r.get('event')in('marker','gtick','guard-arm254','trace-end')for r in rows):raise ValueError('control contains observer')
   continue
  for path in SOURCES[1:5]:
   if m['source_sha256'][Path(path).name]!=spec['pins'][path]:raise ValueError('observer provenance changed')
  ends=[r for r in rows if r.get('event')=='trace-end']
  if len(ends)!=1 or not ends[0]['installed']or any(k.endswith('-dropped')for k in ends[0]['counts']):raise ValueError('observer incomplete')
  if [r['value']for r in rows if r.get('event')=='marker']!=public:raise ValueError('markers missing')
  if (len(fog_rows(rows))!=618 or words_digest(fog_rows(rows))!=spec['follower_sha256']or
   len(target_rows(rows))!=624 or words_digest(target_rows(rows))!=spec['target_sha256']):raise ValueError('retained retail trajectory changed')
  if timer_rows(rows)!=spec['timers']:raise ValueError('guard timer producers changed')
  ranges=[[r['c'],r['result'],r['range']]for r in rows if r.get('event')=='guard-range254']
  if ranges!=spec['ranges']:raise ValueError('guard range changed')
  for r in rows:
   if r.get('event')=='guard-range254'and(r['range']!=0x44160000 or r['anchor']!=[0x44c40000,0x43900000]):raise ValueError('captured guard defaults changed')
  starts=[r for r in rows if r.get('event')=='point-start254'and r['c']==1482]
  if len(starts)!=1 or starts[0]['point']!=[0x44c40000,0x43900000]or '0x4980ba'not in starts[0]['chain']or '0x60d12'not in starts[0]['chain']:raise ValueError('restart did not originate in Attack timer')
  stopped=[r for r in rows if r.get('event')=='point-stop254'and r['c']==1482]
  if len(stopped)!=3 or any(r['after'][:4]!=[1111779984,1100621067,0,0]for r in stopped):raise ValueError('between-owner stop changed')
 return dict(captures=2,controls=1,public_markers=645,raw_follower_rows=618,raw_target_rows=624,guard_arms=len(spec['timers'])*2)
def validate(spec):
 if(spec['version']!=1 or spec['task']!='TARGET-03.2'or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or spec['engine_last_counter']!=1723 or len(spec['instructions'])!=374 or set(spec['pins'])!=set(SOURCES)):raise ValueError('guard contract changed')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source changed '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 if digest(ROOT/HEADER)!=spec['retained_header_sha256']:raise ValueError('old fixture rewritten')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));verify_runtime(bundle,spec);return bundle
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(FIXTURE.read_text());bundle=validate(spec);original_bytes(a.binary,dict(spec['instructions']))
 import verify_wc3_pathing_work242 as runner
 saved=runner.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:
  runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=saved
 result=dict(passed=True,binary_sha256=SHA,fixture_sha256=digest(FIXTURE),test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),**verify_runtime(bundle,spec),instructions=len(spec['instructions']),engine_last_counter=spec['engine_last_counter'],engine=engine,limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
