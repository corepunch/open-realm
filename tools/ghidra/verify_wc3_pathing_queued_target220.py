#!/usr/bin/env python3
"""Verify genuine queued Move fallback against repeated read-only retail captures."""
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
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-queued-target220-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCES=['tools/frida/research/target220_'+s for s in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/ghidra/research/Target220Evidence.java']
TESTS=['wc3_movement.target220_queued_visible_move_retains_issue_point_and_falls_back_on_loss',
 'wc3_movement.target220_retained_packet_point_survives_removed_target_without_rebinding_widgets']
INVALID=[0xffffffff,0xffffffff]
POINT=[0x44c82976,0x448526c6]

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(T220 [^"\r\n]*)" \)',preload)
def parse_marker(value):
 m=re.fullmatch(r'T220 tick=(\d+) scene=(\d+) label=(\w+) order=(\d+) x=(-?[\d.]+) y=(-?[\d.]+)',value)
 if not m:raise ValueError('bad public marker')
 tick,scene,label,order,x,y=m.groups();return dict(tick=int(tick),scene=int(scene),label=label,order=int(order),x=x,y=y)
def public_window(values):
 return [v for v in values if (p:=parse_marker(v))['scene'] in range(1,7) and p['tick']%240>=80]

def validate(spec):
 if (spec['version']!=1 or spec['task']!='TARGET-03.2' or spec['game_sha256']!=SHA or
     spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES)):
  raise ValueError('queued target contract differs')
 for path,expected in spec['pins'].items():
  if digest(ROOT/path)!=expected:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle differs')
 return spec

def native_cases(rows):
 cases=[]
 for scene in range(1,7):
  local=[r for r in rows if r.get('scene')==scene]
  admissions=[r for r in local if r['event']=='append-enter' and r['order']['command']==851986 and r['order']['flags']&4]
  if len(admissions)!=1:raise ValueError('one genuine Shift admission required per scene')
  a=admissions[0];packet=a['order'];identity=packet['identity'];unit=a['state']['unit']
  if(packet['target']==INVALID or packet['point']!=POINT or a['state']['count']!=1 or
     a['state']['head']['identity']==identity or a['state']['task']['code']!=852331):
   raise ValueError('missing live target or queued point at admission')
  leave=next((r for r in local if r['event']=='append-leave' and r['seq']>a['seq']),None)
  if(not leave or leave['state']['count']!=2 or leave['state']['tail']!=packet or leave['state']['head']!=a['state']['head']):
   raise ValueError('queue changed the executing head')
  dispatches=[r for r in local if r['event']=='dispatch-head' and r['order']['identity']==identity]
  if len(dispatches)!=1:raise ValueError('missing queued activation')
  d=dispatches[0]
  preceding=[parse_marker(r['value'])for r in rows if r['event']=='marker'and r['seq']<d['seq']]
  at=preceding[-1]['tick']%240
  if not 80<=at<225:raise ValueError('activation did not occur after mutation')
  tasks=[]
  for r in local:
   if r['seq']<=d['seq']:continue
   if r['event']=='marker':break
   if r['event'] in ('point-task','target-task'):tasks.append(r)
  if scene<6:
   if(len(tasks)!=1 or tasks[0]['event']!='point-task' or tasks[0]['code']!=852331 or
      tasks[0]['point']!=packet['point'] or tasks[0]['unit']!=unit or tasks[0]['caller']!='5fd95d'):
    raise ValueError('lost target did not use captured packet point')
  else:
   if not tasks or any(r['event']!='target-task' or r['target']!=packet['target'] or r['unit']!=unit for r in tasks):
    raise ValueError('visible target did not retain Follow identity')
  cases.append(dict(scene=scene,point=packet['point'],activation_tick=at,
    tasks=[{k:v for k,v in r.items()if k not in ('seq','unit','target','scene')}for r in tasks]))
 return cases

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:
  raise ValueError('repeated original and unhooked control required')
 cases=[];windows=[]
 for c in captures:
  rows,preload=c['rows'],c['preload'];meta,footer=rows[0],rows[-1]
  values=markers(preload)
  if(meta['event']!='metadata' or meta['task']!=spec['task'] or meta['sha256']!=SHA or
     not meta['owned'] or meta['env']not in ('B','C') or meta['source_sha256']['map']!=spec['map_sha256'] or
     footer['event']!='preload-file' or not footer.get('complete') or footer['markers']!=len(values) or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or
     any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows)):
   raise ValueError('failed, incomplete or unowned capture')
  for p in SOURCES[:6]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs')
  if any(meta['source_sha256'][k]!=v for k,v in spec['helper_sha256'].items()):raise ValueError('input helper differs')
  window=public_window(values)
  if window!=spec['public_window']:raise ValueError('public movement or mutation differs')
  windows.append(window)
  inputs=[r for r in rows if r['event']=='player-input']
  if(len(inputs)!=7 or any(r['rc']or not r['plan']['shift']or r['plan']['key']!='move'for r in inputs)):
   raise ValueError('genuine Shift Move inputs required')
  if meta['mode']=='control':
   if any('seq'in r or r['event']=='trace-end'for r in rows):raise ValueError('instrumented control')
   continue
  if [r['value']for r in rows if r['event']=='marker'][4:]!=values:
   raise ValueError('live and preload marker disagreement')
  ends=[r for r in rows if r['event']=='trace-end']
  if(len(ends)!=1 or ends[0].get('readOnly')is not True or
     dict(Counter(r['event']for r in rows if 'seq'in r))!=ends[0]['counts']):raise ValueError('observer completion differs')
  case=native_cases(rows)
  if case!=spec['cases']:raise ValueError('native queue/task contract differs')
  cases.append(case)
 return dict(captures=2,controls=1,scenes=6,admissions=12,fallbacks=10,public_markers=len(windows[0])*3)

def run_engine(binary,data,report):
 results={}
 for edition in ('classic','tft'):
  log=report.with_name(report.stem+'-'+edition+'.log');junit=log.with_suffix('.xml')
  cmd=[str(binary.resolve()),'-data',str(data.resolve())]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test','wc3_movement.target220*']
  with log.open('w')as out:p=subprocess.run(cmd,cwd=ROOT,env=dict(os.environ,TEST_JUNIT=str(junit)),stdout=out,stderr=subprocess.STDOUT,timeout=180)
  totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
  if p.returncode or len(totals)!=1:raise ValueError('engine run failed')
  passed,total,count=map(int,totals[0]);suite=ET.parse(junit).getroot()
  if(passed<900 or passed!=total or count!=len(TESTS) or
     {t.attrib['name']for t in suite.findall('testcase')}!=set(TESTS) or
     any(suite.attrib[k]!='0'for k in ('failures','errors','skipped'))):raise ValueError('empty or incomplete engine checks')
  results[edition]=dict(tests=count,assertions=passed,log_sha256=digest(log),junit_sha256=digest(junit))
 return results

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--binary',type=Path,required=True);ap.add_argument('--report',type=Path,required=True)
 ap.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');ap.add_argument('--data',type=Path,default=ROOT/'build/tests');args=ap.parse_args()
 if args.report.exists():ap.error('report must be new')
 args.report.parent.mkdir(parents=True,exist_ok=True)
 spec=validate(json.loads(FIXTURE.read_text()));result=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
 result.update(passed=True,instructions=original_bytes(args.binary,spec['instructions']),engine=run_engine(args.test_binary,args.data,args.report),
  exclusions=spec['exclusions'],binary_sha256=SHA,fixture_sha256=digest(FIXTURE),test_binary_sha256=digest(args.test_binary),
  game_library_sha256=digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'))
 args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
