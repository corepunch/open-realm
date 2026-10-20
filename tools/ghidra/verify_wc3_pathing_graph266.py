#!/usr/bin/env python3
"""Populated original point factory graphs and engine-owned task retirement."""
import argparse,hashlib,itertools,json,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-graph266-1.27.json'
SOURCES=['tools/ghidra/research/graph266_oracle.py','tools/ghidra/research/Graph266Evidence.java',
         'tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_spell.graph266*','wc3_spell.approach185*','wc3_spell.unit_target_approach*',
       'wc3_spell.committed_cast_cancels_unit_target_approach_and_move','wc3_order_lifecycle.*',
       'wc3_order_reentry.*','wc3_interrupt.*','wc3_save.rejects_prior_save_versions']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def validate(spec):
 if(spec['version']!=1 or spec['task']!='ORDER-04.2'or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS or
    len(spec['instructions'])!=spec['instruction_count']):raise ValueError('graph contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('graph source changed '+p)
 keys=list(itertools.product(('COrderPoint','CTaskPoint'),(False,True),(False,True),(False,True)))
 rows=spec['rows']
 if len(rows)!=16 or [(r['name'],r['nested'],r['null_slots'],r['extra_child_reference'])for r in rows]!=keys:raise ValueError('graph domains differ')
 for row in rows:
  nested=row['nested'];count=11 if nested else 9
  if row['objects']!=count or len(row['phases'])!=2:raise ValueError('missing graph lifetime')
  destroyed=[0,8]+([10,9]if nested else [])+[7,6,5]
  returned=([10,9]if nested else [])+[8,7,6,5,0]
  for cycle,phase in enumerate(row['phases']):
   factory=0 if cycle else count+1 if row['name']=='COrderPoint'else 2
   prepare=1 if cycle else factory+1+int(nested)
   lifetime=prepare if cycle else prepare+1
   if(phase['destroy_order']!=destroyed or phase['child_return_order']!=returned or
      phase['complete_return_order']!=returned+[1,2,3,4] or
      phase['borrowed_survivors']!=4 or phase['final_live']!=0 or
      phase['zero_pending']is not True or phase['seh_balanced']is not True or
      (phase['factory_imports'],phase['preparation_imports'],phase['lifetime_imports'])!=(factory,prepare,lifetime)):raise ValueError('graph cleanup/reuse differs')
 return spec
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=validate(json.loads(FIXTURE.read_text()));original_bytes(a.binary,spec['instructions'])
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import graph266_oracle as original
 rows=original.original(a.binary)
 if rows!=spec['rows']:raise ValueError('fresh original graph differs')
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,task=spec['task'],original_graphs=len(rows),lifetimes=32,
             factory_objects=sum(r['objects']*2 for r in rows),borrowed_survivors=128,
             instructions=len(spec['instructions']),engine=engine,binary_sha256=SHA,
             fixture_sha256=digest(FIXTURE),test_binary_sha256=digest(a.test_binary),
             game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
