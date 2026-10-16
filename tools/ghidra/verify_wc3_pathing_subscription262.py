#!/usr/bin/env python3
"""Verify original explicit-Attack availability suppression and saved ownership."""
from retail_capture_source import matches_capture_source
import argparse,gzip,hashlib,json
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from verify_wc3_pathing_availability260 import projection
import verify_wc3_pathing_work242 as runner
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-subscription262-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/subscribe262_'+n for n in ('capture.py','observer.js','probe.j','make_map.py')]+[
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/ghidra/research/Subscription262Evidence.java']
TESTS=['wc3_order_lifecycle.*','wc3_save.*','wc3_movement.group_move_attack*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):
 import re
 return re.findall(r'call Preload\( "(S262 [^"\r\n]*)" \)',text)
def validate(s):
 if s['version']!=1 or s['task']!='GROUP-03.2' or s['game_sha256']!=SHA or s['engine_tests']!=TESTS or set(s['pins'])!=set(SOURCES):raise ValueError('subscription contract differs')
 for p,sha in s['pins'].items():
  if not matches_capture_source(ROOT,p,sha):raise ValueError('changed source '+p)
 if digest(BUNDLE)!=s['bundle_sha256']:raise ValueError('capture bundle changed')
 if len(s['instructions'])!=194 or len(s['markers'])!=10:raise ValueError('incomplete original coverage')
 return s
def runtime(bundle,s):
 captures=bundle['captures']
 if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:raise ValueError('repeat/control missing')
 for c in captures:
  rows=c['rows'];meta,end=rows[0],rows[-1]
  if meta['sha256']!=SHA or not meta['owned'] or meta['env']not in ('B','C') or meta['source_sha256']!={**s['capture_sources'],'map':s['map_sha256']}:raise ValueError('capture provenance differs')
  if any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows):raise ValueError('incomplete capture')
  if end!={'event':'preload-file','sha256':hashlib.sha256(c['preload'].encode()).hexdigest(),'markers':10,'complete':True} or markers(c['preload'])!=s['markers']:raise ValueError('public control differs')
  if meta['mode']=='control':
   if projection(rows) or any(r.get('event')=='module'for r in rows):raise ValueError('control instrumented')
   continue
  if projection(rows)!=s['events']:raise ValueError('observed guard/clock stream differs')
  actor=[r for r in rows if r.get('event')=='notify-enter' and r['receiver']['identity']==1]
  if [r['tick']for r in actor]!=[2,4,4,6,6,8,8]:raise ValueError('owner deliveries differ')
  if [bool(r['ability_flags']&0x4000)for r in actor]!=[False,True,True,True,True,False,False]:raise ValueError('explicit owner gate differs')
  if [r['tick']for r in rows if r.get('event')=='begin'and r['receiver']==1]!=[4,6]:raise ValueError('suppressed owner acquired exemption')
  if sum(r.get('event')=='notify-enter'for r in rows)!=21:raise ValueError('incomplete notification stream')
 return dict(captures=2,controls=1,public_markers=30,notifications=42,actor_deliveries=14,actor_exemptions=4)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 s=validate(json.loads(FIXTURE.read_text()));original_bytes(a.binary,s['instructions']);live=runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),s)
 a.report.parent.mkdir(parents=True,exist_ok=True);runner.TESTS=TESTS
 result=dict(passed=True,binary_sha256=SHA,**live,instructions=len(s['instructions']),engine=runner.run_engine(a.test_binary,a.data,a.report),game_library_sha256=runner.digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
