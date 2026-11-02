#!/usr/bin/env python3
"""Verify Haunted Mine completion ownership against original captures and engine regressions."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-build297-1.27.json'
SOURCES=['tools/frida/research/build297_'+s for s in ('make_map.py','probe.j','observer.js')]+['tools/frida/research/captain_enrollment157_capture.py','tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/Build297Evidence.java','tools/ghidra/wc3_pathfinding_engine_tests.py']
TESTS=['wc3_building.*','wc3_order_lifecycle.*','wc3_order_subscribers.*','wc3_interrupt.*','wc3_save.*','wc3_movement.pause*']
EVENTS=('begin-enter','begin-leave','complete-enter','complete-leave','parent-retire-enter','parent-retire-leave','completion-order-create','completion-order-admit')
INVALID=[0xffffffff,0xffffffff]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def public(s):return re.findall(r'call Preload\( "(B297[^"\r\n]*)" \)',s)
def validate_semantics(spec):
 events=spec['native'];marks=[dict(re.findall(r'(\w+)=([^ ]+)',s))for s in spec['public']]
 for scene in range(12):
  own=[x for x in events if x['scene']==scene];pub=[x for x in marks if int(x['scene'])==scene]
  begun=[x for x in own if x['event']=='begin-enter'];bound=[x for x in own if x['event']=='begin-leave']
  if len(begun)!=(2 if scene==8 else 1)or len(bound)!=len(begun):raise ValueError('missing begin')
  if begun[0]['retained']!=INVALID or begun[0]['overlayParent']is None or bound[0]['retained']!=begun[0]['structure']['identity']:raise ValueError('missing retained overlay identity')
  completed=[x for x in own if x['event']=='complete-enter'];created=[x for x in own if x['event']=='completion-order-create']
  issued=[x for x in pub if x['label'].startswith('issued-target-852018-')]
  if scene in(5,6,10):
   if completed or created or issued:raise ValueError('retired structure/worker completed')
   continue
  if len(completed)!=1:raise ValueError('missing completion')
  c=completed[0];finish=[x for x in pub if x['label']=='construct-finish']
  if len(finish)!=1 or int(finish[0]['tick'])!=c['tick']:raise ValueError('finish timing')
  retained=scene in(0,3,7,9)
  if (c['retained']!=INVALID)!=retained:raise ValueError('replacement/pause retention')
  allowed=scene in(0,3,7)
  if len(created)!=int(allowed)or len(issued)!=int(allowed):raise ValueError('completion Harvest count')
  if allowed:
   q=created[0]
   if q['code']!=0xd0032 or q['target']['identity']!=c['retained']or q['caller']!='0x543fae':raise ValueError('not target Harvest')
   if c['unit']['flags']&4:raise ValueError('AI gate unexpectedly exercised')
  after=next(x for x in own if x['event']=='complete-leave')
  if after['retained']!=(c['retained']if scene==9 else INVALID):raise ValueError('wrong inverse')
 if len([x for x in events if x['event']=='parent-retire-enter'])!=1:raise ValueError('missing parent death')
 return dict(captures=2,controls=1,public_markers=5814,native_boundaries=104,scenes=12)
def validate(spec):
 if spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_tests']!=TESTS or len(spec['instructions'])!=184:raise ValueError('contract changed')
 for n,h in spec['source_sha256'].items():
  if digest(ROOT/n)!=h:raise ValueError('source changed '+n)
 if digest(SPEC.with_suffix('.json.gz'))!=spec['bundle_sha256']:raise ValueError('bundle changed')
 captures=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()))
 if len(captures)!=3:raise ValueError('missing repeat/control')
 for i,c in enumerate(captures):
  rows=c['rows'];meta=rows[0];footer=rows[-1];pub=public(c['preload'])
  if meta.get('task')!='ORDER-01.16'or meta.get('mode')!=('control'if i==2 else'observe')or not meta.get('owned')or meta.get('sha256')!=SHA or (meta.get('display'),meta.get('remote'))not in((':98','127.0.0.1:27049'),(':99','127.0.0.1:27050'))or meta['source_sha256']['map']!=spec['map_sha256']or footer.get('event')!='preload-file'or not footer.get('complete')or footer.get('markers')!=1938 or hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']or pub!=spec['public']or len(pub)!=1938 or any(x.get('type')=='error'for x in rows):raise ValueError('incomplete/unowned/public capture')
  for n in SOURCES:
   name=Path(n).name
   if name in meta['source_sha256']and meta['source_sha256'][name]!=spec['source_sha256'][n]:raise ValueError('captured source changed')
  if i==2:
   if any(x.get('event')in EVENTS+('module','marker','trace-end')for x in rows):raise ValueError('instrumented control')
  else:
   if [x['value']for x in rows if x.get('event')=='marker']!=pub:raise ValueError('public divergence')
   ends=[x for x in rows if x.get('event')=='trace-end']
   if len(ends)!=1 or not ends[0].get('readOnly')or ends[0]['counts']!=spec['counts']:raise ValueError('observer incomplete')
   if [x for x in rows if x.get('event')in EVENTS]!=spec['native']or len(spec['native'])!=52:raise ValueError('native completion changed')
   for scene in range(12):
    finish=next((x for x in rows if x.get('event')=='marker'and x.get('scene')==scene and 'label=construct-finish 'in x['value']),None)
    cb=next((x for x in rows if x.get('event')=='complete-enter'and x['scene']==scene),None)
    if cb and(not finish or finish['seq']>=cb['seq']):raise ValueError('finish callback ordering')
 return validate_semantics(spec)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());retail=validate(spec);original_bytes(a.binary,spec['instructions'])
 import wc3_pathfinding_engine_tests as runner
 engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 report=dict(passed=True,status='verified',retail=retail,instructions=len(spec['instructions']),engine=engine,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),scope=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
