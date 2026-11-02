#!/usr/bin/env python3
"""Ground target spell current-head contract from unchanged Frida185 captures."""
import argparse,hashlib,json,re,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-spell267-1.27.json'
BASE=ROOT/'tools/ghidra/fixtures/retail-spell-approach185-1.27.json'
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_spell_approach185.h'
SOURCES=['tools/ghidra/research/Spell267Evidence.java','tools/frida/research/spell185_expected.py',
         str(BASE.relative_to(ROOT)),str(HEADER.relative_to(ROOT))]
TESTS=['wc3_spell.owner267*','wc3_spell.approach185*','wc3_spell.range184*',
       'wc3_spell.unit_target_approach*','wc3_spell.graph266*','wc3_movement.spell185*',
       'wc3_order_reentry.*','wc3_order_lifecycle.*','wc3_interrupt.*','wc3_save.rejects_prior_save_versions']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def heads(markers):
 out=[]
 for marker in markers:
  fields=dict(v.split('=',1)for v in marker.split()[1:])
  if 'order'in fields:out.append([int(fields['s']),int(fields['l']),fields['label'],int(fields['order'])])
 return out
def expected_heads():
 out=[]
 for scene in range(3):
  order=851986 if scene==1 else 852092
  out.extend([[scene,0,'created',0],[scene,0,'accepted',order]])
  out.extend([scene,tick,'sample',order if scene==1 or tick<(37 if scene==0 else 40)else 0]for tick in range(1,121))
 return out
def validate(spec):
 if(spec['version']!=1 or spec['task']!='ORDER-01.13' or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES) or spec['engine_tests']!=TESTS or
    spec['heads']!=expected_heads()or len(spec['instructions'])!=spec['instruction_count']):
  raise ValueError('spell head contract differs')
 for source,sha in spec['pins'].items():
  if digest(ROOT/source)!=sha:raise ValueError('spell evidence changed '+source)
 if spec['base_sha256']!=digest(BASE):raise ValueError('original capture expectations changed')
 return spec
def verify_live(archive,spec):
 sys.path.insert(0,str(ROOT/'tools/frida/research'));import spell185_expected as live
 base=json.loads(BASE.read_text());result=live.verify(archive,base,HEADER)
 for pin in base['captures']:
  actual=live.extract(archive/pin['file'])
  if heads(actual['markers'])!=spec['heads']:raise ValueError('retail public head differs')
 return dict(captures=4,observed_repeats=3,controls=1,public_head_queries=1464,
             raw_owner_visits=result['raw_owner_visits'],motion_rows=result['motion_rows'])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--archive',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
 p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=validate(json.loads(FIXTURE.read_text()));original_bytes(a.binary,spec['instructions'])
 live=verify_live(a.archive,spec)
 import verify_wc3_pathing_work242 as engine
 before=engine.TESTS
 try:engine.TESTS=TESTS;checks=engine.run_engine(a.test_binary,a.data,a.report)
 finally:engine.TESTS=before
 result=dict(passed=True,task=spec['task'],**live,instructions=len(spec['instructions']),engine=checks,
             binary_sha256=SHA,fixture_sha256=digest(FIXTURE),test_binary_sha256=digest(a.test_binary),
             game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
