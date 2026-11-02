#!/usr/bin/env python3
"""Check the complete scripted-owner inverse during pending unit removal."""
import argparse,hashlib,json
from pathlib import Path
import verify_wc3_pathing_removal304 as removal
from verify_wc3_pathing_captain358 import original_bytes
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-pause379-1.27.json'
TESTS=['wc3_movement.pause379*','wc3_movement.removal304*','wc3_movement.scope300*',
       'wc3_movement.pause287*','wc3_movement.pause288*','wc3_movement.pause289*',
       'wc3_unit_releases.*']

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def native(bundle):
    result=[]
    for index,capture in enumerate(bundle['captures'][:2]):
        for scene in (2,3):
            for event in ('remove-enter','remove-exit','inactivity-enter'):
                rows=[r for r in capture['rows']if r.get('scene')==scene and r.get('event')==event]
                if len(rows)!=1:raise ValueError('missing paused removal boundary')
                owners=[{k:owner[k]for k in ('classGetter','disabled')}for owner in rows[0]['state']['abilities']]
                if [o['classGetter']for o in owners]!=['0x55c520','0x5e66f0','0x4992a0','0x5fcd40']:
                    raise ValueError('native attached-owner inventory changed')
                expected=[1,1,0]if event=='remove-enter'else[0,0,0]
                if any(o['disabled']!=expected for o in owners):raise ValueError('incomplete scripted-owner inverse')
                result.append(dict(observation=index,scene=scene,event=event,owners=owners))
    return result

def validate(spec):
    if (spec.get('version')!=1 or spec.get('task')!='SEP-01.2' or spec.get('game_sha256')!=removal.SHA or
        spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=506):raise ValueError('wrong owner-release contract')
    for name,expected in spec['pins'].items():
        if digest(ROOT/name)!=expected:raise ValueError('source changed '+name)
    prior=json.loads(removal.SPEC.read_text());bundle=removal.validate(prior)
    if native(bundle)!=spec['projection']:raise ValueError('paused owner projection changed')
    return bundle,prior

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
    p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
    if a.report.exists():p.error('fresh report required')
    spec=json.loads(SPEC.read_text());bundle,prior=validate(spec);original_bytes(a.binary,spec['instructions'])
    import wc3_pathfinding_engine_tests as runner
    engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
    result=dict(passed=True,status='verified',binary_sha256=removal.SHA,instructions=506,
        observations=2,controls=1,public_markers=108,paused_observations=4,owner_snapshots=48,
        original_bundle_sha256=prior['bundle_sha256'],engine=engine,scope=spec['scope'])
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
