#!/usr/bin/env python3
"""Verify ordinary point admission cleanup using unchanged original Stop284 evidence."""
import argparse,hashlib,json
from pathlib import Path
import verify_wc3_pathing_stop284 as retail
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-scope299-1.27.json'
TESTS=['wc3_movement.scope299*','wc3_movement.stop284*','wc3_movement.stop285*',
 'wc3_movement.stop247*','wc3_movement.stop257*','wc3_movement.stop258*',
 'wc3_order_reentry.*','wc3_order_subscribers.*','wc3_order_lifecycle.*',
 'wc3_point214.*','wc3_interrupt.*','wc3_building.build298*',
 'wc3_movement.pause*','wc3_save.*','wc3_movement.public_move*','wc3_movement.public_group*','wc3_movement.public_stop*',
 'wc3_movement.public_overlap*','wc3_movement.public_spawn_move*',
 'wc3_movement.public_mover*','wc3_movement.group*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate(spec):
 if spec['version']!=1 or spec['task']!='MAP-04.2' or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS:
  raise ValueError('point cleanup contract differs')
 evidence=ROOT/spec['evidence']
 if spec['evidence']!='tools/ghidra/fixtures/retail-stop284-1.27.json' or digest(evidence)!=spec['evidence_sha256']:
  raise ValueError('original evidence replaced')
 if set(spec['source_sha256'])!={'tools/ghidra/research/Scope299Evidence.java'} or len(spec['instructions'])!=370:
  raise ValueError('incomplete Ghidra evidence')
 for p,h in spec['source_sha256'].items():
  if digest(ROOT/p)!=h:raise ValueError('Ghidra source differs')
 old=json.loads(evidence.read_text());bundle=retail.validate(old)
 for scene in('2','10'):
  projection=old['projection'][scene]
  if [row[4]for row in projection]!=[0,1,3,3,3]:raise ValueError('missing separate Move recovery')
 return old,bundle

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
 p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(SPEC.read_text());old,bundle=validate(spec)
 original_bytes(a.binary,spec['instructions']);original_bytes(a.binary,old['instructions'])
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:
  runner.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True);engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,status='verified',task=spec['task'],**retail.validate_runtime(bundle,old),instructions=len(spec['instructions']),engine=engine,
  game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),binary_sha256=SHA,limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
