#!/usr/bin/env python3
"""Verify complete retail Warp point-task families and current engine regressions."""
import argparse,copy,gzip,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-waygate377-1.27.json'
TESTS=['wc3_movement.target377*','wc3_save.rejects_version_214_before_retained_point_task_layout',
 'wc3_waygate.*','wc3_save.waygate_state_and_inflight_approach_round_trip',
 'wc3_save.rejects_invalid_waygate_entity_references','wc3_order_reentry.*','wc3_movement.retail_gate*']
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TRANSPORT={'metadata','module','loading-key','owned-loading-input','trace-end','preload-file','control-start-file'}
def digest(data):return hashlib.sha256(data).hexdigest()
def project(raw,scenes):
 events=[copy.deepcopy(r)for r in raw if r['event']not in TRANSPORT]
 movers={r[k]['mover']for r in events for k in ('before','after')if isinstance(r.get(k),dict)and 'mover'in r[k]}
 if len(movers)!=1 or next(iter(movers))=='0x0':raise ValueError('mover storage inventory')
 actors={r['scene']:(r['pointer'],r['unit']['identity'])for r in events if r['event']=='move-producer-enter'}
 if set(actors)!=set(scenes):raise ValueError('scene inventory')
 for r in events:
  scene=r['scene'];event=r['event']
  for key in ('before','after'):
   if isinstance(r.get(key),dict)and 'mover'in r[key]:
    if r[key]['mover']not in movers:raise ValueError('unknown mover')
    r[key]['mover']='single_observed_mover_storage'
  if event in ('move-producer-enter','move-producer-leave'):
   if r['pointer']!=actors[scene][0]or r['unit']['identity']!=actors[scene][1]:raise ValueError('producer identity')
   r['pointer']={'unit_identity':actors[scene][1]}
  if event in ('point-task','move-arrival'):
   if r['unit']!=actors[scene][0]:raise ValueError('task identity')
   r['unit']={'unit_identity':actors[scene][1]}
  if event=='point-request':
   if int(r['bridge'],16)!=int(actors[scene][0],16)+0x164:raise ValueError('bridge offset')
   r['bridge']={'unit_identity':actors[scene][1],'offset':0x164}
 return events

def validate_cohort(cohort):
 from wc3_waygate377_cohort import project as cohort_project,verify as cohort_proof
 captures=cohort['captures']
 if len(captures)!=3:raise ValueError('two cohort observations and control required')
 rows=[[json.loads(line)for line in c['raw_jsonl'].splitlines()]for c in captures]
 modes=[next(r for r in raw if r['event']=='metadata')['mode']for raw in rows]
 if modes!=['observe','observe','control']:raise ValueError('cohort capture roles')
 public=lambda c:re.findall(r'call Preload\( "(T377C [^"\r\n]*)" \)',c['preload'])
 full=public(captures[0])
 if len(full)!=288 or full!=public(captures[2]):raise ValueError('complete cohort public control stream')
 for i in (0,1):
  if [r['value']for r in rows[i]if r['event']=='marker']!=public(captures[i]):raise ValueError('cohort raw/preload marker mismatch')
 scoped=lambda c:[v for v in public(c)if int(re.search(r'tick=(\d+)',v)[1])>=40]
 if len(scoped(captures[0]))!=243 or not(scoped(captures[0])==scoped(captures[1])==scoped(captures[2])):raise ValueError('complete cohort interval public markers')
 first,second=(cohort_project(r)for r in rows[:2])
 if len(first)!=3139 or first!=second:raise ValueError('complete cohort interval records')
 cohort_proof(first)
 return dict(cohort_semantic_records=3139,cohort_interval_public_markers=243,cohort_public_control_markers=288)

def validate_runtime(bundle):
 if len(bundle.get('families',[]))!=2:raise ValueError('both native families required')
 records=markers=0
 for family,(scenes,want_rows,want_markers)in zip(bundle['families'],[([0,1,2,3],2908,464),([0,1,2,3,4],3791,581)]):
  if family['scenes']!=scenes or len(family['captures'])!=3:raise ValueError('full capture inventory')
  captures=family['captures'];rows=[[json.loads(line)for line in c['raw_jsonl'].splitlines()]for c in captures]
  public=lambda c:re.findall(r'call Preload\( "(T377[^"]*)" \)',c['preload'])
  expected=public(captures[2])
  if len(expected)!=want_markers:raise ValueError('control marker count')
  if next(r for r in rows[2]if r['event']=='metadata')['mode']!='control':raise ValueError('unhooked control required')
  for i in (0,1):
   if [r['value']for r in rows[i]if r['event']=='marker']!=expected or public(captures[i])!=expected:raise ValueError('complete public control mismatch')
  first,second=(project(r,scenes)for r in rows[:2])
  if len(first)!=want_rows or first!=second:raise ValueError('whole ordered transaction mismatch')
  codes={r['code']for r in first if r['event']=='move-producer-enter'}
  if codes!={851971,851986}:raise ValueError('literal command inventory')
  if 4 in scenes and next(r for r in first if r['event']=='move-producer-enter'and r['scene']==4)['code']!=851971:raise ValueError('Smart retarget attribution')
  records+=want_rows;markers+=want_markers
 cohort=validate_cohort(bundle['cohort'])
 return dict(observations=6,controls=3,public_markers=markers+288,semantic_records=records+3139,**cohort)

def validate(spec):
 if spec.get('version')!=2 or spec.get('task')!='TARGET-01.2'or spec.get('game_sha256')!=SHA or spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=2345:raise ValueError('wrong point-task contract')
 for name,expected in spec['pins'].items():
  if digest((ROOT/name).read_bytes())!=expected:raise ValueError('source pin changed '+name)
 raw=(ROOT/spec['bundle']).read_bytes()
 if digest(raw)!=spec['bundle_sha256']:raise ValueError('raw bundle changed')
 bundle=json.loads(gzip.decompress(raw));result=validate_runtime(bundle)
 if len(bundle['ghidra'])!=3:raise ValueError('all saved exports required')
 for export in bundle['ghidra']:
  if export['export']!=export['readback']or digest(export['export'].encode())!=export['sha256']:raise ValueError('saved Ghidra readback changed')
  decoded=json.loads(export['export'])
  if len(decoded['functions'])!=14 or len(decoded['instructions'])!=2009 or any(spec['instructions'].get(k)!=v for k,v in decoded['instructions'].items()):raise ValueError('complete original instruction inventory')
 export=bundle['cohort']['ghidra']
 if export['export']!=export['readback']or digest(export['export'].encode())!=export['sha256']:raise ValueError('cohort saved Ghidra readback')
 decoded=json.loads(export['export'])
 if len(decoded['functions'])!=19 or decoded['instructions']!=spec['instructions']:raise ValueError('complete cohort instruction inventory')
 return bundle,result

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());bundle,result=validate(spec)
 from verify_wc3_pathing_captain358 import original_bytes
 original_bytes(a.binary,spec['instructions'])
 import wc3_pathfinding_engine_tests as runner
 engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 result.update(passed=True,status='verified',binary_sha256=SHA,instructions=2345,engine=engine,scope=spec['scope'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
