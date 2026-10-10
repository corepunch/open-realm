#!/usr/bin/env python3
"""Verify Attack TargetLost validation and point recovery, repeated retail observations and engine state."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes

ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-attack-recovery223-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCES=['tools/frida/research/target223_'+s for s in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/ghidra/research/Target223Evidence.java']
TESTS=['wc3_movement.target223_loss_during_attack_chase_retains_head_for_point_recovery',
 'wc3_movement.target223_invisibility_validates_detection_and_shared_vision_synchronously',
 'wc3_movement.target223_point_recovery_freezes_queried_pose_through_save_stop_and_queue',
 'wc3_movement.target223_attack_subscriptions_rebuild_in_order_and_skip_new_outer_registrations',
 'wc3_movement.target223_cyclone_releases_attack_subscription_and_physical_owner',
 'wc3_movement.target223_replacement_attack_releases_previous_physical_target']
INVALID=[0xffffffff,0xffffffff]

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(T223 [^"\r\n]*)" \)',preload)
def validate(spec):
 if (spec['version']!=1 or spec['task']!='TARGET-03.2' or spec['game_sha256']!=SHA or
     spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES) or len(spec['markers'])!=999):
  raise ValueError('Attack chase contract differs')
 for path,expected in spec['pins'].items():
  if digest(ROOT/path)!=expected:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle differs')
 return spec

def native_cases(rows):
 cases=[]
 for scene in range(5):
  local=[r for r in rows if r.get('scene')==scene]
  loss=[r for r in local if r['event']=='attack-lost' and r['unit']['owner']==0 and 60<=r['tick']%200<100]
  if len(loss)!=1:raise ValueError('one subject Attack TargetLost required')
  subject=loss[0]['unit']['identity']
  events=[r for r in local if r.get('unit') and r['unit']['identity']==subject]
  validation=[r for r in events if r['event']=='validate' and r['caller']=='49b5b7']
  if len(validation)!=1 or validation[0]['visibility']!=0 or validation[0]['result']!=[0xdd,0,0xaa,0xdd,0xaa][scene]:
   raise ValueError('detection-only TargetLost validation differs')
  points=[r for r in events if r['event']=='point-task' and r['caller']=='49d490']
  recover=[r for r in events if r['event']=='recover' and r['caller']=='49b594']
  if scene==1:
   if points or recover:raise ValueError('TrueSight must retain the target chase')
  else:
   if(len(points)!=1 or len(recover)!=1 or points[0]['code']!=0xd016c or points[0]['range']!=1126891520 or
      not recover[0]['flags']&0x40000 or points[0]['unit']['head']!=loss[0]['unit']['head'] or
      points[0]['unit']['head']==INVALID or not validation[0]['seq']<recover[0]['seq']<points[0]['seq']):
    raise ValueError('ordered Attack recovery task or retained public head differs')
   release=[r for r in events if r['event']=='release-target' and recover[0]['seq']<r['seq']<points[0]['seq']]
   if len(release)!=1:raise ValueError('target release must precede point creation')
  cases.append(dict(scene=scene,subject=subject,
   loss=[[r['c'],r['caller'],r['flags'],r['target'],r['unit']['head'],r['unit']['task']]for r in loss],
   validation=[[r['c'],r['visibility'],r['result'],r['targetUnit']]for r in validation],
   recovery=[[r['c'],r['flags'],r['target'],r['unit']['head'],r['unit']['task']]for r in recover],
   point=[[r['c'],r['code'],r['point'],r['range'],r['unit']['head'],r['unit']['task']]for r in points]))
 return cases

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:
  raise ValueError('repeated original and unhooked control required')
 for c in captures:
  rows,preload=c['rows'],c['preload'];meta,footer=rows[0],rows[-1];values=markers(preload)
  if(meta['event']!='metadata' or meta['task']!=spec['task'] or meta['sha256']!=SHA or
     not meta['owned'] or meta['env']not in ('B','C') or meta['source_sha256']['map']!=spec['map_sha256'] or
     footer['event']!='preload-file' or not footer.get('complete') or footer['markers']!=len(values) or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or values!=spec['markers'] or
     any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows)):
   raise ValueError('failed, incomplete or unowned capture')
  for p in SOURCES[:6]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs')
  if any(meta['source_sha256'][k]!=v for k,v in spec['helper_sha256'].items()):raise ValueError('input helper differs')
  if meta['mode']=='control':
   if any('seq'in r or r['event']=='trace-end'for r in rows):raise ValueError('instrumented control')
   continue
  if [r['value']for r in rows if r['event']=='marker'][3:]!=values:
   raise ValueError('live and preload marker disagreement')
  ends=[r for r in rows if r['event']=='trace-end']
  if(len(ends)!=1 or ends[0].get('readOnly')is not True or
     dict(Counter(r['event']for r in rows if 'seq'in r))!=ends[0]['counts']):raise ValueError('observer completion differs')
  if native_cases(rows)!=spec['cases']:raise ValueError('native Attack chase contract differs')
 return dict(captures=2,controls=1,scenes=5,public_markers=2997)

def run_engine(binary,data,report):
 results={}
 for edition in ('classic','tft'):
  log=report.with_name(report.stem+'-'+edition+'.log');junit=log.with_suffix('.xml')
  env=dict(os.environ,TEST_JUNIT=str(junit),LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
  args=[str(binary),'-data',str(data)]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test','wc3_movement.target223*']
  with log.open('w')as out:subprocess.run(args,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=120,check=True)
  suite=ET.parse(junit).getroot();tests=suite.findall('.//testcase')
  totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
  if(len(totals)!=1 or int(totals[0][0])<100 or totals[0][0]!=totals[0][1] or
     {t.attrib['name']for t in tests}!=set(TESTS) or len(tests)!=len(TESTS) or
     any(suite.attrib[k]!='0'for k in ('failures','errors','skipped'))):
   raise ValueError('missing or failed engine chase test')
  results[edition]=dict(tests=len(tests),assertions=int(totals[0][0]),log_sha256=digest(log),junit_sha256=digest(junit))
 return results

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--binary',type=Path,required=True)
 ap.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
 ap.add_argument('--data',type=Path,default=ROOT/'build/tests');ap.add_argument('--report',type=Path,required=True)
 args=ap.parse_args();spec=validate(json.loads(FIXTURE.read_text()))
 if args.report.exists():ap.error('report must be new')
 proof=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
 original_bytes(args.binary,spec['instructions']);args.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**proof,instructions=len(spec['instructions']),
  engine=run_engine(args.test_binary,args.data,args.report),fixture_sha256=digest(FIXTURE),
  bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(args.test_binary),
  limits=spec['limits'],game_library_sha256=digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'))
 args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
