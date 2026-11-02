#!/usr/bin/env python3
"""Verify retail construction-start stage and nested callback ownership."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-build295-1.27.json'
SOURCES=['tools/frida/research/build295_'+s for s in ('make_map.py','probe.j','observer.js')]+['tools/frida/research/captain_enrollment157_capture.py','tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/Build295Evidence.java','tools/ghidra/wc3_pathfinding_engine_tests.py']
TESTS=['wc3_building.*','wc3_order_lifecycle.*','wc3_order_subscribers.*','wc3_interrupt.*','wc3_save.*']
EVENTS=('human-repair-enter','human-repair-leave','admit-enter','admit-leave','build-native-enter','build-native-leave','repair-work-enter','repair-work-leave','inside-work-enter','inside-work-leave','inside-end-enter','inside-end-leave')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def public(s):return re.findall(r'call Preload\( "(B295[^"\r\n]*)" \)',s)
def native(rows):return[x for x in rows if x.get('event')in EVENTS]
def validate(spec):
 if(spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_tests']!=TESTS or len(spec['instructions'])!=841):raise ValueError('Build contract changed')
 for n,h in spec['source_sha256'].items():
  if digest(ROOT/n)!=h:raise ValueError('source changed '+n)
 if digest(SPEC.with_suffix('.json.gz'))!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 captures=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()))
 if len(captures)!=3:raise ValueError('missing observed repeat/control')
 for i,c in enumerate(captures):
  rows=c['rows'];meta=rows[0];footer=rows[-1];pub=public(c['preload'])
  if(meta.get('task')!='ORDER-01.16'or meta.get('mode')!=('control'if i==2 else'observe')or not meta.get('owned')or meta.get('sha256')!=SHA or
     (meta.get('display'),meta.get('remote'))not in((':98','127.0.0.1:27049'),(':99','127.0.0.1:27050'))or meta['source_sha256']['map']!=spec['map_sha256']or
     footer.get('event')!='preload-file'or not footer.get('complete')or footer.get('markers')!=1270 or
     hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']or pub!=spec['public']or len(pub)!=1270 or
     any(x.get('type')=='error'for x in rows)):
   raise ValueError('incomplete/unowned/public capture changed')
  for n in SOURCES:
   name=Path(n).name
   if name in meta['source_sha256']and meta['source_sha256'][name]!=spec['source_sha256'][n]:raise ValueError('captured source changed')
  if i==2:
   if any(x.get('event')in EVENTS+('marker','module','trace-end')for x in rows):raise ValueError('instrumented control')
  else:
   if[x['value']for x in rows if x.get('event')=='marker']!=pub:raise ValueError('observer/public divergence')
   ends=[x for x in rows if x.get('event')=='trace-end']
   if len(ends)!=1 or not ends[0].get('readOnly')or ends[0]['counts']!=spec['counts']:raise ValueError('observer incomplete')
   if native(rows)!=spec['native']or len(spec['native'])!=282:raise ValueError('native Build ownership changed')
 # Assert the meaning of the frozen stream as well as its exact provenance.
 parsed=[dict(re.findall(r'(\w+)=([^ ]+)',x))for x in spec['public']]
 def mark(scene,label):
  found=[x for x in parsed if int(x['scene'])==scene and x['label']==label]
  if len(found)!=1:raise ValueError('missing unique '+label)
  return found[0]
 for scene in range(25):
  kind=scene%5;action=scene//5
  raw=int.from_bytes((('h','o','u','e','n')[kind]+'B95').encode(),'big')
  worker=int.from_bytes((('h','o','u','e','n')[kind]+'W95').encode(),'big')
  accepted=mark(scene,'build-accepted');rejected=mark(scene,'bad-build-rejected');begin=mark(scene,'construct-start')
  if accepted['order']!=str(raw)or rejected['label']!='bad-build-rejected':raise ValueError('Build admission changed')
  if(begin['order']!=str(raw)or begin['hidden']!=str(int(kind in(1,3,4)))or begin['worker']!=str(worker)or
     begin['building']!=str(raw)or float(begin['buildlife'])!=(500,600,550,600,500)[kind]):raise ValueError('start stage changed')
  if action:
   callback=mark(scene,'callback-accepted'if action<3 else'callback-return')
   expected=0 if action==4 else raw if kind in(1,3,4)or action==3 else(851986 if action==1 else 0)
   if callback['order']!=str(expected)or float(callback['buildlife'])!=float(begin['buildlife']):raise ValueError('nested callback changed')
  if kind==0 and action in(0,1,2,4):
   sample=[x for x in parsed if int(x['scene'])==scene and x['label']=='sample'and int(x['tick'])%45==20]
   if len(sample)!=1:raise ValueError('missing Human work sample')
   sample=sample[0]
   if sample['order']!=str(851986 if action==1 else 852024 if action in(0,2)else 0):raise ValueError('Human callback owner overwritten')
   if action in(1,4)and float(sample['buildlife'])!=50:raise ValueError('paused Human construction advanced')
 # Actual Human work starts in normal/Stop only, after the public start marker.
 events=spec['native'];humans=[x for x in events if x['event']=='human-repair-enter']
 repairs=[x for x in events if x['event']=='repair-work-enter']
 if [x['tick']//45 for x in humans]!=[0,5,10,15,20]or [x['tick']//45 for x in repairs]!=[0,10]:raise ValueError('Human work entry order changed')
 for x in events:
  if x['event']=='inside-work-enter'and x['unit']['depth']!=1+int(x['tick']//45>=20):raise ValueError('world presence stage changed')
  if x['event']=='inside-work-leave'and x['unit']['depth']!=2+int(x['tick']//45>=20):raise ValueError('inside work stage changed')
 return dict(captures=2,controls=1,public_markers=3810,native_boundaries=564)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());retail=validate(spec);original_bytes(a.binary,spec['instructions'])
 import wc3_pathfinding_engine_tests as runner
 engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 report=dict(passed=True,status='verified',retail=retail,instructions=len(spec['instructions']),engine=engine,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),scope=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
