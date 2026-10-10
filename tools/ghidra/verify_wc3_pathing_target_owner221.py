#!/usr/bin/env python3
"""Verify target owner-change recovery, repeated retail observations and engine state."""
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
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-target-owner221-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCES=['tools/frida/research/target221_'+s for s in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/ghidra/research/Target221Evidence.java']
TESTS=['wc3_movement.target221_public_owner_transfer_retires_follow_synchronously']
INVALID=[0xffffffff,0xffffffff]

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(T221 [^"\r\n]*)" \)',preload)
def parse_marker(value):
 m=re.fullmatch(r'T221 tick=(\d+) scene=(\d+) label=(\w+) order=(\d+) owner=(\d+) x=(-?[\d.]+) y=(-?[\d.]+)',value)
 if not m:raise ValueError('bad public marker')
 tick,scene,label,order,owner,x,y=m.groups()
 return dict(tick=int(tick),scene=int(scene),label=label,order=int(order),owner=int(owner),x=x,y=y)

def validate(spec):
 if (spec['version']!=1 or spec['task']!='TARGET-03.2' or spec['game_sha256']!=SHA or
     spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES) or len(spec['markers'])!=496):
  raise ValueError('target owner contract differs')
 for path,expected in spec['pins'].items():
  if digest(ROOT/path)!=expected:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle differs')
 return spec

def native_cases(rows):
 cases=[]
 for scene in range(7):
  before=next(r for r in rows if r.get('event')=='marker' and r.get('scene')==scene and 'label=before_transfer 'in r['value'])
  after=next(r for r in rows if r.get('event')=='marker' and r.get('scene')==scene and 'label=after_transfer 'in r['value'])
  events=[r for r in rows if before['seq']<r.get('seq',0)<after['seq']]
  enters=[r for r in events if r['event']=='owner-enter'];leaves=[r for r in events if r['event']=='owner-leave']
  handlers=[r for r in events if r['event']=='owner-handler-enter'];ends=[r for r in events if r['event']=='owner-handler-leave']
  if(len(enters)!=1 or len(leaves)!=1 or enters[0]['caller']!='21552f' or
     enters[0]['unit']['identity']!=leaves[0]['unit']['identity'] or
     leaves[0]['unit']['owner']!=enters[0]['next'] or any(r['event']=='target-lost'for r in events)):
   raise ValueError('owner transition or independent TargetLost differs')
  start,end=parse_marker(before['value']),parse_marker(after['value'])
  if start['order']!=(851986 if scene==3 else 851971) or start['tick']%70!=20:
   raise ValueError('wrong command or transfer point')
  if len(handlers)!=(0 if scene in (2,6)else 1) or len(ends)!=len(handlers):
   raise ValueError('owner subscription delivery differs')
  if handlers:
   h,e=handlers[0],ends[0]
   local=[r for r in events if h['seq']<r['seq']<e['seq']]
   if(h['target']!=enters[0]['unit']['identity'] or h['unit']['identity']!=e['unit']['identity'] or
      h['caller']!='5fdc81' or h['unit']['head']==INVALID or h['unit']['task']==INVALID):
    raise ValueError('wrong subscriber or retained target')
   if [r['event']for r in local[:3]]!=['clear-target','recover','arrival'] or [r['caller']for r in local[:3]]!=['5fdfdf','5fdfe8','5fb403']:
    raise ValueError('target clear/recovery/arrival ordering differs')
   if scene==4:
    if(e['target']!=h['target'] or e['unit']['head']!=h['unit']['head'] or
       e['unit']['task']in (INVALID,h['unit']['task']) or end['order']!=start['order']):
     raise ValueError('approach did not advance to Follow continuation')
   elif e['target']!=INVALID or e['unit']['head']!=INVALID or e['unit']['task']!=INVALID or end['order']!=0:
    raise ValueError('persistent Follow did not complete')
  elif end['order']!=(851971 if scene==2 else 0):
   raise ValueError('same-owner or self-transfer outcome differs')
  if start['x']!=end['x'] or start['y']!=end['y']:raise ValueError('transfer changed committed position')
  cases.append(dict(scene=scene,next=enters[0]['next'],handlers=len(handlers),
   outcome='advance'if scene==4 else'retain'if scene==2 else'end',
   events=[r['event']for r in events]))
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
  if native_cases(rows)!=spec['cases']:raise ValueError('native owner-change contract differs')
 return dict(captures=2,controls=1,scenes=7,deliveries=10,public_markers=1488)

def run_engine(binary,data,report):
 results={}
 for edition in ('classic','tft'):
  log=report.with_name(report.stem+'-'+edition+'.log');junit=log.with_suffix('.xml')
  env=dict(os.environ,TEST_JUNIT=str(junit),LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
  args=[str(binary),'-data',str(data)]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test','wc3_movement.target221*']
  with log.open('w')as out:subprocess.run(args,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=120,check=True)
  suite=ET.parse(junit).getroot();tests=suite.findall('.//testcase')
  totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
  if(len(totals)!=1 or int(totals[0][0])<135 or totals[0][0]!=totals[0][1] or
     {t.attrib['name']for t in tests}!=set(TESTS) or len(tests)!=len(TESTS) or
     any(suite.attrib[k]!='0'for k in ('failures','errors','skipped'))):
   raise ValueError('missing or failed engine owner test')
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
