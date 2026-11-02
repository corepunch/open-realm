#!/usr/bin/env python3
"""Verify retained GROUP-01.2 raw transitions and the finite public-producer gate."""
import argparse,base64,gzip,hashlib,json,os,re,subprocess,sys
from pathlib import Path
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-group012-1.27.json'
BUNDLE=ROOT/'tools/ghidra/fixtures/retail-group012-native-1.27.json.gz'
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
EXPECTED=[('U0',None,'G0'),('U1',None,'G1'),('U1','G1',None),('U1',None,'G2'),
          ('U0','G0',None),('U0',None,'G3'),('U1','G2','G3'),('U1','G3',None),('U0','G3',None)]
TESTS=[
'wc3_movement.group012_independent_shift_preserves_nine_transitions_and_four_route_owners',
'wc3_bot.captain_large_roster_preserves_admission_batches_and_cold_save',
'wc3_movement.selected238_alt_callbacks_keep_candidate_order_and_replacement',
'wc3_movement.selected_independent_shift_inputs_match_original_complete_journeys',
'wc3_movement.selected237_ground_and_flight_share_primary_request',
'wc3_movement.selected238_alt_owns_separate_flight_requests',
'wc3_movement.selected239_singleton_clears_player_request_history',
'wc3_movement.selected244_complete_mixed_alt_shift_motion',
'wc3_movement.partition235_public_order_partitions_and_saves_physical_owners',
'wc3_movement.partition235_captain_split_preserves_shared_owner_and_replacement',
'wc3_api.group_point_order_forms_admit_twelve_members',
'wc3_api.group_point_order_ability_ownership',
'wc3_movement.public_captain_thirteen_mixed_matches_original_complete_journey',
'wc3_movement.public_captain_thirteen_mixed_matches_original_largest_stop_after_shared',
'wc3_movement.public_captain_thirteen_mixed_matches_original_largest_stop_before_shared',
'wc3_movement.public_captain_thirteen_mixed_matches_original_last_binding_stop',
'wc3_movement.public_captain_removal_withdraws_roster_before_slot_reuse',
'wc3_movement.public_captain_retarget_retains_roster_and_cancels_last_shared_binding',
'wc3_movement.public_captain_stop_retains_roster_and_cancels_last_shared_binding',
'wc3_movement.public_captain_partial_removal_preserves_remaining_roster_and_save',
]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def project(rows):
    if not rows or rows[0].get('event')!='metadata' or rows[0].get('sha256')!=SHA or not rows[0].get('owned'):
        raise ValueError('original capture ownership/binary differs')
    if rows[-1].get('event')!='trace-end' or not rows[-1].get('installed'):
        raise ValueError('incomplete original trace')
    if any(r.get('type')=='error' or r.get('event')=='trace-failed' for r in rows):raise ValueError('capture error')
    if not any(r.get('event')=='marker' and 'tick=300 label=complete ' in r.get('value','') for r in rows):
        raise ValueError('missing complete public boundary')
    participants={m['mover'] for r in rows if r.get('event')=='pair-group-phase-begin' for m in r['members']}
    movers={};groups={};pointers={};events=[]
    def identity(raw):
        if raw in ([-1,-1],[0xffffffff,0xffffffff]):return None
        key=tuple(raw)
        if key not in groups:groups[key]='G'+str(len(groups))
        return groups[key]
    for ordinal,row in enumerate(rows):
        if row.get('event')!='move-group-bind' or row['mover'] not in participants or row['before']==row['after']:continue
        if row['mover'] not in movers:movers[row['mover']]='U'+str(len(movers))
        before,after=identity(row['before']),identity(row['after'])
        if after is not None:
            if row['group'] in pointers and pointers[row['group']]!=after:raise ValueError('physical pointer rebound to another identity')
            pointers[row['group']]=after
        events.append(dict(ordinal=ordinal,member=movers[row['mover']],before=before,after=after,caller=row['caller']))
    targets=[dict(ordinal=i,group=pointers[r['group']],path_pointer=r['path'],target=r['target'])
             for i,r in enumerate(rows) if r.get('event')=='group-target' and r['group'] in pointers]
    if len(participants)!=2 or len(groups)!=4 or len(pointers)!=4 or len(targets)!=4:
        raise ValueError('missing member/owner/path association')
    if len({x['path_pointer']for x in targets})!=4 or len({x['group']for x in targets})!=4:
        raise ValueError('route ownership alias')
    if [(e['member'],e['before'],e['after'])for e in events]!=EXPECTED:raise ValueError('nine original transitions differ')
    if [e['caller']for e in events]!=[1488887,1488887,1512373,1488887,1512373,1488887,1488887,1512373,1512373]:
        raise ValueError('binding/retirement caller differs')
    return dict(transitions=events,group_identity_aliases={str(k):v for k,v in groups.items()},
                mover_pointer_aliases=movers,group_pointer_aliases=pointers,group_path_associations=targets,
                rows=len(rows),motion_commits=sum(r.get('event')=='velocity-commit'for r in rows))
def validate_runtime(bundle,spec):
    if len(bundle['captures'])!=2 or len(spec['captures'])!=2:raise ValueError('missing original repeat')
    outputs=[]
    for capture,expected in zip(bundle['captures'],spec['captures']):
        raw=base64.b64decode(capture['raw_base64'],validate=True)
        if capture['name']!=expected['name'] or hashlib.sha256(raw).hexdigest()!=expected['raw_sha256']:
            raise ValueError('literal original capture differs')
        rows=[json.loads(line)for line in raw.decode().splitlines()];actual=project(rows)
        if actual!=expected['projection']:raise ValueError('original ordinals/identity aliases/path associations differ')
        outputs.append(actual)
    semantics=[[{k:v for k,v in e.items()if k!='ordinal'}for e in x['transitions']]for x in outputs]
    if semantics[0]!=semantics[1]:raise ValueError('transition repeat differs')
    return dict(captures=2,transitions_per_capture=9,physical_owners_per_capture=4,
                original_rows=[x['rows']for x in outputs],motion_commits=[x['motion_commits']for x in outputs])
def validate(spec):
    if spec.get('version')!=1 or spec.get('task')!='GROUP-01.2' or spec.get('game_sha256')!=SHA or spec.get('engine_tests')!=TESTS:
        raise ValueError('producer transition contract differs')
    for path,sha in spec['pins'].items():
        if digest(ROOT/path)!=sha:raise ValueError('changed source '+path)
    if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('raw capture bundle differs')
    return validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
def run_engine(binary,data,report,spec):
    results={}
    for edition in ('classic','tft'):
        tests=assertions=0
        for i,name in enumerate(TESTS):
            log=report.with_name(report.stem+'-'+edition+'-'+str(i)+'.log');xml=log.with_suffix('.xml')
            env=dict(os.environ,TEST_JUNIT=str(xml));env.pop('TEST_SHARD',None)
            env['LD_LIBRARY_PATH']=str(binary.resolve().parent.parent/'lib')+os.pathsep+'/GitHub/wc3-analysis/native-sdl2'+os.pathsep+env.get('LD_LIBRARY_PATH','')
            args=[str(binary.resolve()),'-data',str(data.resolve())]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test',name]
            with log.open('w')as stream:subprocess.run(args,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=300,check=True)
            suite=ET.parse(xml).getroot();totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
            if len(totals)!=1 or totals[0][0]!=totals[0][1] or int(totals[0][2])!=1 or name not in {t.attrib['name']for t in suite.findall('.//testcase')} or any(suite.attrib[k]!='0'for k in ('failures','errors','skipped')):
                raise ValueError('missing or failed actual producer test '+name)
            tests+=1;assertions+=int(totals[0][0])
        results[edition]=dict(tests=tests,assertions=assertions)
        if results[edition]!=spec['engine_expected']:raise ValueError('engine gate totals differ')
    return results
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');parser.add_argument('--data',type=Path,default=ROOT/'build/tests');parser.add_argument('--report',type=Path,required=True);a=parser.parse_args()
    if a.report.exists():parser.error('fresh report required')
    sys.path.insert(0,str(ROOT/'tools/ghidra'));from verify_wc3_pathing_target_normalize218 import original_bytes
    spec=json.loads(FIXTURE.read_text());native=validate(spec);original_bytes(a.binary,spec['instructions']);a.report.parent.mkdir(parents=True,exist_ok=True)
    result=dict(passed=True,task=spec['task'],native=native,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report,spec),
                fixture_sha256=digest(FIXTURE),binary_sha256=SHA,module_sha256=digest(a.test_binary.resolve().parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
