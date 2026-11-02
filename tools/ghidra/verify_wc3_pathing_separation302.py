#!/usr/bin/env python3
"""Verify counted removal separation using unchanged original204 captures."""
import argparse,gzip,hashlib,json,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/frida/research'))
import removal204_verify as previous
SPEC=ROOT/'tools/ghidra/fixtures/retail-separation302-1.27.json'
EVIDENCE='tools/ghidra/fixtures/retail-removal-separation204-1.27.json.gz'
SOURCE='tools/ghidra/research/Separation302Evidence.java'
TESTS=['wc3_repulsion_policy.*','wc3_movement.scope301*','wc3_movement.scope300*',
       'wc3_ability_dispatch.*','wc3_order_lifecycle.*','wc3_building.*',
       'wc3_unit_releases.*','wc3_save.*',
       'wc3_pathfinding.spatial_publication_follows_changed_objects',
       'wc3_e2e212.wrapped_requests_and_reused_members_keep_live_successors']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def transitions(bundle):
    scenes=[]
    for start,row in enumerate(bundle['stages']):
        if row['event']!='remove-begin':continue
        end=next(i for i in range(start+1,len(bundle['stages'])) if bundle['stages'][i]['event']=='destroy')
        steps=[]
        for step in bundle['stages'][start:end+1]:
            if step['event']in('acquire-end','release-end'):
                unit=step['unit'];steps.append([step['event'],unit['depth'],unit['f54'],unit['sep']])
        scenes.append(dict(initial_depth=row['unit']['depth'],steps=steps))
    if len(scenes)!=3 or [x['initial_depth']for x in scenes]!=[0,0,1]:
        raise ValueError('missing ordinary, nested and paused removal')
    expected=[dict(initial_depth=0,steps=[['acquire-end',1,0,None],['release-end',0,0,0x11110000]]),
              dict(initial_depth=0,steps=[['acquire-end',1,0,None],['release-end',0,0,0x10110000]]),
              dict(initial_depth=1,steps=[['release-end',0,0,0x10110000],['acquire-end',1,0,None],['release-end',0,0,0x10110000]])]
    if scenes!=expected:raise ValueError('counted removal transition changed')
    return scenes
def validate(spec):
    if spec['version']!=1 or spec['task']!='SEP-01.2' or spec['game_sha256']!=SHA or spec['save_version']!=185:
        raise ValueError('unsupported removal separation contract')
    if spec['engine_tests']!=TESTS or spec['source_sha256']!={SOURCE:digest(ROOT/SOURCE)}:
        raise ValueError('construction/source contract changed')
    if spec['evidence']!=EVIDENCE or spec['evidence_sha256']!=digest(ROOT/EVIDENCE):
        raise ValueError('original204 evidence substituted')
    if spec['function_count']!=9 or len(spec['instructions'])!=spec['instruction_count'] or not spec['instructions']:
        raise ValueError('missing original instruction evidence')
    bundle=json.loads(gzip.decompress((ROOT/EVIDENCE).read_bytes()))
    previous.validate(bundle)
    if spec['retail_transitions']!=transitions(bundle):raise ValueError('retail transition fixture changed')
    return bundle
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    if a.report.exists():p.error('new report required')
    spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
    sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
    from verify_separation_eligibility import verify
    predicate=verify(a.binary)
    if predicate!=json.loads((ROOT/'tools/ghidra/fixtures/retail-separation-eligibility-1.27.json').read_text()):
        raise ValueError('unchanged original eligibility expectations differ')
    import verify_wc3_pathing_work242 as runner
    before=runner.TESTS
    try:
        runner.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
        engine=runner.run_engine(a.test_binary,a.data,a.report)
    finally:runner.TESTS=before
    result=dict(passed=True,status='verified',task=spec['task'],retail_captures_reused=True,
        retail_observations=2,retail_controls=1,retail_transitions=transitions(bundle),
        original_predicate_cases=len(predicate['cases']),instructions=len(spec['instructions']),
        engine=engine,save_version=185,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),
        binary_sha256=SHA,limits=spec['limits'])
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
