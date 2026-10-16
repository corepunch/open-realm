#!/usr/bin/env python3
"""Verify two ordered availability-range guards against complete retail captures."""
import argparse, gzip, hashlib, json, re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes, SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-guard270-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
TESTS=['wc3_order_lifecycle.*','wc3_interrupt.*','wc3_order_reentry.*',
       'wc3_movement.group_move_attack*','wc3_save.rejects_prior_save_versions']
SOURCES=['tools/frida/research/guard270_'+s for s in ('probe.j','observer.js','make_map.py')]+[
 'tools/frida/research/rank264_capture.py','tools/frida/research/group032_make_map.py',
 'tools/ghidra/research/Guard270Evidence.java']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(G270 [^"\r\n]*)" \)',text)
def validate(spec):
 if spec['game_sha256']!=SHA:raise ValueError('original differs')
 if set(spec['pins'])!=set(SOURCES):raise ValueError('source inventory differs')
 if spec['tests']!=TESTS:raise ValueError('engine checks differ')
 if len(spec['instructions'])!=434 or not set(('6f49e130','6f498e50','6f498520','6f66f030','6f5fb040','6f4985c0'))<=set(spec['instructions']):raise ValueError('incomplete original inventory')
 for path,sha in spec['pins'].items():
  if digest(ROOT/path)!=sha:raise ValueError('source differs: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('raw bundle differs')
 return spec
def verify_live(bundle,spec):
 observed=[]
 for name in ('observe-b1','observe-b2'):
  rows=bundle[name];meta=rows[0]
  if meta['sha256']!=SHA or meta['mode']!='observe' or not meta['owned'] or meta['source_sha256']['map']!=spec['map_sha256']:raise ValueError('capture provenance')
  if meta['source_sha256']['guard270_observer.js']!=spec['pins']['tools/frida/research/guard270_observer.js']:raise ValueError('observer source differs')
  if [r['minimum']for r in rows if r['event']=='module']!=[0x42000000]:raise ValueError('native minimum differs')
  if len([r for r in rows if r['event']=='preload-file' and r['complete'] and r['markers']==25])!=1:raise ValueError('public completion missing')
  end=[r for r in rows if r['event']=='trace-end']
  if len(end)!=1 or not end[0]['readOnly'] or end[0]['depth'] or not end[0]['installed']:raise ValueError('observer incomplete')
  if any(r['event'] in ('trace-failed','error') for r in rows):raise ValueError('capture failed')
  stream=[r for r in rows if 'seq'in r]
  if len(stream)!=175:raise ValueError('ordered stream incomplete')
  # Pairwise same-lane notifications, with raw world-coordinate words.
  notifications=[];current=None
  for row in stream:
   if row['event']=='notify':
    current=None
    if row['actor']['x']==0x44000000 and row['actor']['y']==row['candidate']['y']:
     current=dict(actor=row['actor'],candidate=row['candidate'],events=[]);notifications.append(current)
   elif current is not None:current['events'].append(row)
  if len(notifications)!=5:raise ValueError('missing public lane')
  for lane,notification in enumerate(notifications):
   events=notification['events'];ranges=[r for r in events if r['event']=='range' and r['caller'] in ('49e256','49e2b7')]
   expected=spec['lanes'][lane]
   if [[r['caller'],r['range'],r['mode'],r['result']]for r in ranges]!=expected['guards']:raise ValueError('range guard differs')
   if any(r['event']=='exempt' for r in events)!=expected['exempt']:raise ValueError('exemption before rejection')
   if notification['actor']['move']!=expected['move']:raise ValueError('Move lifetime differs')
  observed.append(stream)
 if observed[0]!=observed[1]:raise ValueError('retail repeat differs')
 control=bundle['control-b1']
 if control[0]['mode']!='control' or any('seq'in r for r in control):raise ValueError('hooked control')
 if control[0]['source_sha256']['map']!=spec['map_sha256']:raise ValueError('control map differs')
 expected=markers(bundle['preloads']['observe-b1'])
 if len(expected)!=25 or not any('label=complete lane=4'in m for m in expected):raise ValueError('incomplete public timeline')
 for name in ('observe-b2','control-b1'):
  if markers(bundle['preloads'][name])!=expected:raise ValueError('public control differs')
 return dict(captures=3,public_markers=75,ordered_events=350,public_lanes=10)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True)
 a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=validate(json.loads(FIXTURE.read_text()));original_bytes(a.binary,spec['instructions'])
 live=verify_live(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
 import verify_wc3_pathing_work242 as engine
 previous=engine.TESTS
 try:engine.TESTS=TESTS;checks=engine.run_engine(a.test_binary,a.data,a.report)
 finally:engine.TESTS=previous
 result=dict(passed=True,task='GROUP-03.2',**live,instructions=len(spec['instructions']),engine=checks,
  fixture_sha256=digest(FIXTURE),test_binary_sha256=digest(a.test_binary),
  game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
