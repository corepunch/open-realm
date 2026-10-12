#!/usr/bin/env python3
"""Verify retained fine identities through public death/removal cleanup."""
import argparse,json
from pathlib import Path
import verify_wc3_pathing_scope300 as previous
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-scope301-1.27.json'
SOURCE='tools/ghidra/research/Scope301Evidence.java'
EVIDENCE='tools/ghidra/fixtures/retail-scope300-1.27.json'
TESTS=['wc3_movement.scope301*']+previous.TESTS+[
 'wc3_movement.target218*','wc3_movement.target220*','wc3_movement.target223*',
 'wc3_movement.target256*','wc3_movement.follow278*','wc3_unit.*',
 'wc3_ability_dispatch.*','wc3_unit_releases.*',
 'wc3_e2e212.wrapped_requests_and_reused_members_keep_live_successors',
 'wc3_pathfinding.spatial_publication_follows_changed_objects']
digest=previous.digest
EMPTY=[-128000]*4

def validate_runtime(bundle):
 for capture in bundle['captures'][:2]:
  rows=capture['rows']
  for scene in(4,6,12,14):
   removal=scene%8==4
   inner=[r['fine']for r in rows if r.get('event')=='inner-enter' and r.get('scene')==scene]
   markers=[r for r in rows if r.get('event')=='marker-spatial' and r.get('scene')==scene]
   if len(inner)!=(3 if removal else 4) or len(markers)!=5:raise ValueError('missing death/removal boundary')
   recoveries=[r['caller']for r in rows if r.get('event')=='unit-stop-enter' and r.get('scene')==scene]
   expected=['0x600433']*3+([]if removal else['0x5fc18a'])
   if recoveries!=expected:raise ValueError('unexpected cleanup at physical destruction')
   first=inner[0]
   def check(fine,active,counter):
    if not fine or fine['pointer']!=first['pointer'] or fine['identity']!=first['identity']:
     raise ValueError('fine identity retired prematurely')
    if fine['category']!=(0x010000ca if active else 0xca) or fine['box']!=([19,19,21,21]if active else EMPTY):
     raise ValueError('fine unpublication boundary differs')
    if fine['flags']!=counter:raise ValueError('unbalanced fine exclusion')
   for i,fine in enumerate(inner):check(fine,not removal or i==0,2)
   for i,row in enumerate(markers):
    label=('player-enter','player-exit','unit-event','after','settled')[i]
    if 'label='+label+' 'not in row['value']:raise ValueError('marker order differs')
    if removal and i==4:
     if row['fine']is not None:raise ValueError('physical removal failed')
    else:check(row['fine'],i==0,0)
 return dict(spatial_scenes=8,retail_captures_reused=True)

def validate(spec):
 if spec['version']!=1 or spec['task']!='MAP-04.2' or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS:
  raise ValueError('fine lifetime contract differs')
 if spec['evidence']!=EVIDENCE or digest(ROOT/EVIDENCE)!=spec['evidence_sha256']:
  raise ValueError('original capture contract substituted')
 if spec['source_sha256']!={SOURCE:digest(ROOT/SOURCE)} or len(spec['instructions'])!=spec['instruction_count']:
  raise ValueError('missing original-code evidence')
 if spec['instructions'].get('6fce4588')!='000cfeff'*4 or spec['global_type']!='/WarcraftIII/Pathfinding127/WC3PathIntegerRectangle':
  raise ValueError('inactive rectangle constant differs')
 oldspec=json.loads((ROOT/EVIDENCE).read_text());old,control,bundle=previous.validate(oldspec)
 previous.validate_runtime(bundle,old,control,oldspec);validate_runtime(bundle)
 return oldspec,old,bundle

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
 p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(SPEC.read_text());oldspec,old,bundle=validate(spec)
 for evidence in(spec,oldspec,old):original_bytes(a.binary,evidence['instructions'])
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:
  runner.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True);engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,status='verified',task=spec['task'],**validate_runtime(bundle),
  instructions=len(spec['instructions']),engine=engine,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),
  binary_sha256=SHA,save_version=184,limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
