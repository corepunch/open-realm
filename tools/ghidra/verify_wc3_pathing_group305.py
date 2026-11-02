#!/usr/bin/env python3
"""Verify physical identity restoration without quadratic registry scans.

Reuse frozen Frida queued-binding evidence; no new live save/load capture or
retail behavioral change is claimed. All historical numerical fixtures stay fixed.
"""
import argparse, hashlib, json
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes, SHA
import verify_wc3_pathing_work249 as prior
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-group305-1.27.json'
TESTS=['wc3_save.groups305*','wc3_save.rejects_invalid_physical_group_payloads',
       'wc3_movement.queued249*','wc3_movement.selected242*',
       'wc3_movement.public_captain_thirteen_mixed_matches_original_complete_journey',
       'wc3_movement.formation169*','wc3_movement.physical_owner_order*']
SOURCES=['tools/ghidra/research/Group305Evidence.java',
         'tools/ghidra/fixtures/retail-work249-1.27.json',
         'tools/ghidra/fixtures/retail-work249-1.27.json.gz',
         'games/warcraft-3/game/tests/fixtures/retail_queued_scope249.h']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-04.6' or spec['game_sha256']!=SHA or
    spec['engine_tests']!=TESTS or spec['unit_counts']!=[1,12,256,1024,1600,4096] or
    spec['save_version']!=185 or len(spec['instructions'])!=134 or set(spec['pins'])!=set(SOURCES)):
  raise ValueError('physical identity contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('changed original evidence '+p)
 original_spec=json.loads(prior.FIXTURE.read_text())
 bundle=prior.validate(original_spec)
 return original_spec,bundle

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
 p.add_argument('--data',type=Path,default=ROOT/'build/tests')
 p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(SPEC.read_text());old,bundle=validate(spec)
 original_bytes(a.binary,spec['instructions']);original_bytes(a.binary,old['instructions'])
 import sys
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import work249_oracle as native
 fresh=native.original(a.binary);control=native.original(a.binary,False)
 prior.native_controls(fresh,control)
 if fresh!=bundle['original'] or control!=bundle['controls']:raise ValueError('original binding scopes changed')
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,status='verified',task=spec['task'],instructions=len(spec['instructions']),
             original_cases=len(fresh['rows']),original_controls=len(control['rows']),
             **prior.validate_runtime(bundle,old),unit_counts=spec['unit_counts'],
             engine=engine.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,
             game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),
             limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
