#!/usr/bin/env python3
"""Verify Attack Blink range retention and recovery exclusion, repeated retail observations and engine state."""
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
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-attack-blink224-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCES=['tools/frida/research/target224_'+s for s in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/ghidra/research/Target224Evidence.java',
 'tools/ghidra/fixtures/retail-object-range184-1.27.json',
 'games/warcraft-3/game/tests/fixtures/retail_object_range184.h',
 'tools/ghidra/verify_wc3_pathing_range.py']
TESTS=['wc3_movement.target224_initial_attack_near_blink_retains_but_far_blink_ends',
 'wc3_movement.target224_ordinary_far_relocation_does_not_publish_blink_loss',
 'wc3_movement.target224_committed_and_predicted_ranges_match_original_object_cases',
 'wc3_movement.target224_far_blink_after_range_entry_releases_target_without_point_recovery']
INVALID=[0xffffffff,0xffffffff]

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(T224 [^"\r\n]*)" \)',preload)
def validate(spec):
 if (spec['version']!=1 or spec['task']!='TARGET-03.2' or spec['game_sha256']!=SHA or
     spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES) or len(spec['markers'])!=795 or
     len(spec['instructions'])!=549 or len(spec['cases'])!=4):
  raise ValueError('Attack chase contract differs')
 for path,expected in spec['pins'].items():
  if digest(ROOT/path)!=expected:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle differs')
 return spec

def native_cases(rows):
 cases=[]
 for scene in range(4):
  local=[r for r in rows if r.get('scene')==scene and r.get('tick',0)%200<50]
  loss=[r for r in local if r['event']=='attack-lost' and r['unit']['owner']==0]
  checks=[r for r in local if r['event']=='range-check' and r['caller']=='49b55a']
  if scene==3:
   if loss or checks or any(r['event']=='blink'for r in local):raise ValueError('ordinary relocation is not Blink TargetLost')
   after=[r for r in local if r['event']=='marker' and 'label=relocated 'in r['value']]
   if len(after)!=1 or 'order=851983 'not in after[0]['value']:raise ValueError('relocation must retain Attack')
   cases.append(dict(scene=scene,retained=True));continue
  if len(loss)!=1 or len(checks)!=1:raise ValueError('one Attack Blink range check required')
  subject=loss[0]['unit']['identity'];events=[r for r in local if r.get('unit')and r['unit']['identity']==subject]
  check=checks[0]
  if check['predict']!=0 or check['range']!=0x44fa0000 or check['constant']!=0x44fa0000 or check['result']!=int(scene==0):
   raise ValueError('Blink must test committed centers at original2000 range')
  if not loss[0]['seq']<check['seq']:raise ValueError('range check precedes loss')
  validation=[r for r in events if r['event']=='validate' and r['caller']=='49b5b7']
  recover=[r for r in events if r['event']=='recover' and r['caller']=='49b594']
  points=[r for r in events if r['event']=='point-task' and r['caller']=='49d490']
  release=[r for r in events if r['event']=='release-target' and r['caller']=='49d3ff']
  if points:raise ValueError('Blink window must exclude point capture')
  if scene==0:
   if len(validation)!=1 or validation[0]['result']!=0 or validation[0]['visibility']!=0 or recover or release:
    raise ValueError('near Blink must retain detection-valid Attack')
   if validation[0]['unit']['head']!=loss[0]['unit']['head']:raise ValueError('near Blink changes public head')
  else:
   if(validation or len(recover)!=1 or len(release)!=1 or not recover[0]['flags']&0x40000 or
      not check['seq']<recover[0]['seq']<release[0]['seq']):raise ValueError('far Blink must recover without validation')
   next_sample=next(r for r in rows if r['event']=='marker'and r.get('scene')==scene and r['tick']>loss[0]['tick'])
   if 'order=0 'not in next_sample['value']:raise ValueError('far Blink did not end public Attack')
  cases.append(dict(scene=scene,loss=[[r['c'],r['target'],r['unit']['head'],r['unit']['task']]for r in loss],
   range=[[r['c'],r['range'],r['predict'],r['result'],r['source'],r['target']]for r in checks],
   validation=[[r['c'],r['visibility'],r['result'],r['unit']['head']]for r in validation],
   recovery=[[r['c'],r['flags'],r['unit']['head']]for r in recover],
   release=[[r['c'],r['target'],r['unit']['head']]for r in release]))
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
 return dict(captures=2,controls=1,scenes=4,public_markers=2385)

def run_engine(binary,data,report):
 results={}
 for edition in ('classic','tft'):
  log=report.with_name(report.stem+'-'+edition+'.log');junit=log.with_suffix('.xml')
  env=dict(os.environ,TEST_JUNIT=str(junit),LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
  args=[str(binary),'-data',str(data)]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test','wc3_movement.target224*']
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
