#!/usr/bin/env python3
"""Original spatial-message guard rejection and production receiver lifetime."""
import argparse,hashlib,json,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-bridge265-1.27.json'
SOURCES=['tools/ghidra/research/bridge265_oracle.py','tools/ghidra/research/Bridge265Evidence.java',
         'tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_order_lifecycle.*','wc3_order_reentry.*','wc3_interrupt.*',
       'wc3_spell.range184*','wc3_spell.approach185*','wc3_spell.unit_target_approach*',
       'wc3_movement.completion196*','wc3_movement.spell185*','wc3_save.rejects_prior_save_versions']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def validate(spec):
 import itertools
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
 import bridge265_oracle as original
 if(spec['version']!=1 or spec['task']!='ORDER-04.3'or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS or
    len(spec['instructions'])!=spec['instruction_count']):raise ValueError('bridge contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('bridge source changed '+p)
 keys=list(itertools.product(('positive','negative'),(1,3),original.TAGS,original.STATES))
 rows=spec['rows']
 if len(rows)!=132 or [(r['domain'],r['references'],r['tag'],r['state'])for r in rows]!=keys:raise ValueError('bridge domain matrix differs')
 for row in rows:
  accepted=row['state']=='valid';event=0x40190065 if row['tag']==original.TAGS[0]else 0x40190066
  if(row['dispatched']is not accepted or row['event']!=(event if accepted else 0)or
     row['pin_references']!=([row['references']+1]if accepted else [])or row['balanced']is not True):raise ValueError('bridge delivery/reference balance differs')
 return spec
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=validate(json.loads(FIXTURE.read_text()));original_bytes(a.binary,spec['instructions'])
 import bridge265_oracle as original
 rows=original.original(a.binary)
 if rows!=spec['rows']:raise ValueError('fresh original bridge differs')
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,task=spec['task'],original_cases=len(rows),positive_cases=66,negative_cases=66,
             rejected=120,delivered=12,instructions=len(spec['instructions']),engine=engine,
             binary_sha256=SHA,fixture_sha256=digest(FIXTURE),test_binary_sha256=digest(a.test_binary),
             game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
