#!/usr/bin/env python3
"""Verify Human construction Repair activation against original captures and engine regressions."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-build298-1.27.json'
SOURCES=['tools/frida/research/build298_'+s for s in ('make_map.py','probe.j','observer.js')]+['tools/frida/research/captain_enrollment157_capture.py','tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/Build298Evidence.java','tools/ghidra/wc3_pathfinding_engine_tests.py']
TESTS=['wc3_building.*','wc3_order_lifecycle.*','wc3_order_subscribers.*','wc3_interrupt.*','wc3_save.*','wc3_movement.pause*']
EVENTS=('handoff-enter','handoff-leave','after-build-task-retire','transient-enter','transient-leave','dispatch-enter','dispatch-leave','admit-enter','admit-leave','repair-work-enter','repair-work-leave','repair-stop-enter','repair-stop-leave')
INVALID=[0xffffffff,0xffffffff]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def public(s):return re.findall(r'call Preload\( "(B298[^"\r\n]*)" \)',s)
def validate_semantics(spec):
 events=spec['native'];marks=[dict(re.findall(r'(\w+)=([^ ]+)',s))for s in spec['public']]
 for scene in range(10):
  own=[e for e in events if e['scene']==scene];pub=[e for e in marks if int(e['scene'])==scene]
  h=[e for e in own if e['event']=='handoff-enter'];q=[e for e in own if e['event']=='transient-enter'and e['caller']=='0x544130']
  if len(h)!=1 or len(q)!=1:raise ValueError('missing Human handoff')
  h=h[0];q=q[0];after=next(e for e in own if e['event']=='after-build-task-retire')
  for field in('head','tail','count','task'):
   if h['unit'][field]!=after['unit'][field]:raise ValueError('premature Build retirement')
  if h['unit']['count']!=1 or q['unit']['head']!=h['unit']['head']or q['order']['command']!=852024 or(q['arg1'],q['arg2'])!=(1,1):raise ValueError('wrong Repair insertion')
  leave=next(e for e in own if e['seq']==q['seq']+1)
  if leave['event']!='transient-leave'or leave['unit']['count']!=2 or leave['unit']['head']!=h['unit']['head']or leave['unit']['tail']!=q['order']['identity']:raise ValueError('Repair did not follow Build')
  dispatch=[e for e in own if e['event']=='dispatch-enter'and e['order']['command']==852024]
  issued=[e for e in pub if e['label']=='issued-target-852024']
  if len(dispatch)!=(2 if scene==3 else 1)or len(issued)!=len(dispatch):raise ValueError('Repair notification missing')
  for d in dispatch:
   if d['unit']['count']!=1 or d['unit']['head']!=q['order']['identity']or d['unit']['tail']!=q['order']['identity']or d['unit']['task']!=INVALID:raise ValueError('Build survived Repair activation')
  work=[e for e in own if e['event']=='repair-work-enter']
  if len(work)!=(2 if scene==3 else int(scene<6)):raise ValueError('replacement/removal began work')
  if scene==3:
   suspension=next(e for e in own if e['event']=='dispatch-enter'and e['order']['command']==851973)
   if suspension['unit']['count']!=2 or suspension['unit']['tail']!=q['order']['identity']or dispatch[0]['order']['identity']!=dispatch[1]['order']['identity']:raise ValueError('Repair identity lost on pause')
  if scene==7:
   samples=[e for e in pub if e['label']=='sample'and int(e['tick'])>=704]
   if not samples or any(e['order']!='0'or e['life']!='50.000'for e in samples):raise ValueError('Stop callback failed to cancel work')
 if len(events)!=162:raise ValueError('native boundaries changed')
 return dict(captures=2,controls=1,public_markers=3027,native_boundaries=324,scenes=10,insertions=10)
def validate(spec):
 if spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_tests']!=TESTS or len(spec['instructions'])!=859:raise ValueError('contract changed')
 for n,h in spec['source_sha256'].items():
  if digest(ROOT/n)!=h:raise ValueError('source changed '+n)
 if digest(SPEC.with_suffix('.json.gz'))!=spec['bundle_sha256']:raise ValueError('bundle changed')
 captures=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()))
 if len(captures)!=3:raise ValueError('missing repeat/control')
 for i,c in enumerate(captures):
  rows=c['rows'];meta=rows[0];footer=rows[-1];pub=public(c['preload'])
  if meta.get('task')!='ORDER-01.16'or meta.get('mode')!=('control'if i==2 else'observe')or not meta.get('owned')or meta.get('sha256')!=SHA or (meta.get('display'),meta.get('remote'))not in((':98','127.0.0.1:27049'),(':99','127.0.0.1:27050'))or meta['source_sha256']['map']!=spec['map_sha256']or footer.get('event')!='preload-file'or not footer.get('complete')or footer.get('markers')!=1009 or hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']or pub!=spec['public']or len(pub)!=1009 or any(x.get('type')=='error'for x in rows):raise ValueError('incomplete/unowned/public capture')
  for n in SOURCES:
   name=Path(n).name
   if name in meta['source_sha256']and meta['source_sha256'][name]!=spec['source_sha256'][n]:raise ValueError('captured source changed')
  if i==2:
   if any(x.get('event')in EVENTS+('module','marker','trace-end')for x in rows):raise ValueError('instrumented control')
  else:
   if [x['value']for x in rows if x.get('event')=='marker']!=pub:raise ValueError('public divergence')
   ends=[x for x in rows if x.get('event')=='trace-end']
   if len(ends)!=1 or not ends[0].get('readOnly')or ends[0]['counts']!=spec['counts']:raise ValueError('observer incomplete')
   if [x for x in rows if x.get('event')in EVENTS]!=spec['native']or len(spec['native'])!=162:raise ValueError('native completion changed')
   for scene in range(10):
    own=[e for e in rows if e.get('scene')==scene]
    notifications=[e for e in own if e.get('event')=='marker'and 'label=issued-target-852024 'in e['value']]
    work=[e for e in own if e.get('event')=='repair-work-enter']
    if len(work)>len(notifications)or any(n['seq']>=w['seq']for n,w in zip(notifications,work)):raise ValueError('work preceded Repair notification')
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
