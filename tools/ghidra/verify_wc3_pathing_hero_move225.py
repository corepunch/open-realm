#!/usr/bin/env python3
"""Check frozen retail Hero movement producers, exact return words and engine lifecycle."""
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
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-hero-move225-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCES=['tools/frida/research/move225_'+s for s in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/ghidra/research/Move225Evidence.java']
TESTS=['wc3_hero_movement.move225_agility_delta_survives_speed_override_and_level_changes',
 'wc3_hero_movement.move225_default_query_and_active_motion_keep_distinct_speeds_after_save',
 'wc3_hero_movement.move225_stock_zero_coefficient_preserves_speed']
EVENTS={'hero-contribution','hero-refresh','move-additive-delta','speed-publication','default-speed','current-speed'}
LABELS=['created','level3','agi77','item','set_speed','agi51','drop','level4','strip','move','stop']

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(M225 [^"\r\n]*)" \)',preload)
def normalize(rows):
 out=[]
 for r in rows:
  if r['event'] not in EVENTS:continue
  r=dict(r);r.pop('hero',None);r.pop('handle',None)
  if r.get('unit'):
   r['unit']=dict(r['unit']);r['unit'].pop('identity')
  out.append(r)
 return out

def stages(rows):
 out=[];defaults=[];current=[]
 for r in rows:
  if r['event']=='default-speed':defaults.append(r['result'])
  elif r['event']=='current-speed':current.append(r['result'])
  elif r['event']=='marker':
   m=re.search(r'label=(\w+) .*baseagi=(\d+) agi=(\d+) level=(\d+)',r['value'])
   if m and m[1] in LABELS:
    out.append(dict(label=m[1],default=defaults[-2],current=current[-1],nonhero=defaults[-1],
                    base_agility=int(m[2]),agility=int(m[3]),level=int(m[4])))
 if [s['label']for s in out]!=LABELS:raise ValueError('missing public stage')
 return out

def validate(spec):
 if(spec['version']!=1 or spec['task']!='MOVE-01.1' or spec['game_sha256']!=SHA or
    spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES) or
    set(spec['maps'])!={'stock','custom','fraction'} or len(spec['instructions'])!=277):raise ValueError('Hero contract differs')
 for path,expected in spec['pins'].items():
  if digest(ROOT/path)!=expected:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle differs')
 return spec

def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=9:raise ValueError('nine captures required')
 for name,expected in spec['maps'].items():
  local=[c for c in bundle['captures']if c['map']==name]
  if [c['rows'][0]['mode']for c in local]!=['observe','observe','control']:raise ValueError('repeat/control required')
  if len(expected['markers'])!=75 or len(expected['events'])!=350:raise ValueError('incomplete frozen contract')
  for c in local:
   rows,preload=c['rows'],c['preload'];meta,footer=rows[0],rows[-1];values=markers(preload)
   if(meta['event']!='metadata' or meta['task']!=spec['task'] or meta['sha256']!=SHA or
      not meta['owned'] or meta['env']not in ('B','C') or meta['source_sha256']['map']!=expected['sha256'] or
      footer['event']!='preload-file' or not footer.get('complete') or footer['markers']!=75 or
      hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or values!=expected['markers'] or
      any(r.get('type')=='error' or r['event']=='trace-failed'for r in rows)):
    raise ValueError('failed, incomplete or unowned capture')
   for p in SOURCES[:6]:
    if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs')
   if any(meta['source_sha256'][k]!=v for k,v in spec['helper_sha256'].items()):raise ValueError('input helper differs')
   if meta['mode']=='control':
    if any('seq'in r or r['event']=='trace-end'for r in rows):raise ValueError('instrumented control')
    continue
   if [r['value']for r in rows if r['event']=='marker'][3:]!=values:raise ValueError('live/preload disagreement')
   ends=[r for r in rows if r['event']=='trace-end']
   if(len(ends)!=1 or ends[0].get('readOnly')is not True or
      dict(Counter(r['event']for r in rows if 'seq'in r))!=ends[0]['counts']):raise ValueError('observer completion differs')
   if normalize(rows)!=expected['events'] or stages(rows)!=expected['stages']:raise ValueError('native Hero contract differs')
 return dict(captures=6,controls=3,maps=3,public_markers=675,native_events=2100)

def validate_engine_words(log,spec):
 rows=re.findall(r'M225 engine profile=(\d+) stage=(\d+) agi=(\d+) item=(-?\d+) default=([0-9a-f]{8}) current=([0-9a-f]{8}) cached=([0-9a-f]{8})',log)
 if len(rows)!=18:raise ValueError('missing engine word stages')
 for row,(profile,stage) in zip(rows,((p,s)for p in range(2)for s in range(9))):
  native=spec['maps'][('custom','fraction')[profile]]['stages'][stage]
  if(tuple(map(int,row[:2]))!=(profile,stage) or int(row[2])!=native['agility'] or
     int(row[3])!=native['agility']-native['base_agility'] or int(row[4],16)!=native['default'] or
     int(row[5],16)!=native['current']):raise ValueError('engine differs from frozen native words')
 return len(rows)

def run_engine(binary,data,report,spec):
 results={}
 for edition in ('classic','tft'):
  log=report.with_name(report.stem+'-'+edition+'.log');junit=log.with_suffix('.xml')
  env=dict(os.environ,TEST_JUNIT=str(junit),LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
  args=[str(binary),'-data',str(data)]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test','wc3_hero_movement.move225*']
  with log.open('w')as out:subprocess.run(args,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=120,check=True)
  suite=ET.parse(junit).getroot();tests=suite.findall('.//testcase')
  totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
  if(len(totals)!=1 or int(totals[0][0])<100 or totals[0][0]!=totals[0][1] or
     {t.attrib['name']for t in tests}!=set(TESTS) or len(tests)!=len(TESTS) or
     any(suite.attrib[k]!='0'for k in ('failures','errors','skipped'))):raise ValueError('missing or failed engine Hero test')
  results[edition]=dict(word_stages=validate_engine_words(log.read_text(),spec),tests=len(tests),assertions=int(totals[0][0]),log_sha256=digest(log),junit_sha256=digest(junit))
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
  engine=run_engine(args.test_binary,args.data,args.report,spec),fixture_sha256=digest(FIXTURE),
  bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(args.test_binary),
  limits=spec['limits'],game_library_sha256=digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'))
 args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
