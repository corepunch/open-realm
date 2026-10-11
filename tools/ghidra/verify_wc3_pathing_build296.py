#!/usr/bin/env python3
"""Verify original Undead unit-owned work waits and current engine lifecycle."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-build296-1.27.json'
SOURCES=['tools/frida/research/build296_'+s for s in ('make_map.py','probe.j','observer.js')]+['tools/frida/research/captain_enrollment157_capture.py','tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/Build296Evidence.java','tools/ghidra/wc3_pathfinding_engine_tests.py']
TESTS=['wc3_building.*','wc3_order_lifecycle.*','wc3_order_subscribers.*','wc3_interrupt.*','wc3_save.*','wc3_movement.pause*','wc3_movement.premature_destination_change_keeps_retry_status_without_forced_arrival']
EVENTS=('work-enter','work-leave','wait-enter','wait-leave','request','execute-enter','execute-leave')
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def public(s):return re.findall(r'call Preload\( "(B296[^"\r\n]*)" \)',s)
def native(rows):return[x for x in rows if x.get('event')in EVENTS]
def validate_semantics(spec):
 parsed=[dict(re.findall(r'(\w+)=([^ ]+)',x))for x in spec['public']]
 def mark(scene,tick,label='sample'):
  found=[x for x in parsed if int(x['scene'])==scene and int(x['tick'])==tick and x['label']==label]
  if len(found)!=1:raise ValueError('missing unique work stage')
  return found[0]
 for scene in range(8):
  start=scene*100+7;raw=int.from_bytes(('uC96'if scene==1 else'uB96').encode(),'big')
  if mark(scene,start,'construct-start')['order']!=str(raw):raise ValueError('missing Build head')
  if scene in(0,1,3,5,7):
   if mark(scene,start+19)['order']!=str(raw)or mark(scene,start+20)['order']!='0':raise ValueError('work deadline changed')
  if scene==2:
   if mark(scene,227)['order']!='851973'or mark(scene,235)['order']!='851973'or mark(scene,236)['order']!='0':raise ValueError('pause/resume changed')
  if scene==4 and(mark(scene,427)['order']!='851986'or mark(scene,437)['order']!='851986'):raise ValueError('Move replaced by old work')
  if scene==6 and mark(scene,627)['order']!='0':raise ValueError('Stop replaced by old work')
 events=spec['native']
 for scene in range(8):
  own=[x for x in events if x['scene']==scene]
  work=[x for x in own if x['event']=='work-enter'];wait=[x for x in own if x['event']=='wait-enter'and x['delay']==0x3f800000]
  execute=[x for x in own if x['event']=='execute-enter'and x['request']['delay']==0x3f800000]
  if len(work)!=1 or len(wait)!=1 or len(execute)!=1:raise ValueError('missing work request')
  w=wait[0];e=execute[0];q=e['request']
  if(w['caller']!='0x544458'or w['scale']!=0 or w['unit']['identity']!=work[0]['unit']['identity']or
     q['value']!=0xd019d or q['clock']['time']!=q['deadline']or
     q['flags']!=(65536 if scene in(2,4,6)else 0)):
   raise ValueError('unit request owner/deadline/cancellation changed')
  leave=next(x for x in own if x['seq']==e['seq']+1)
  if leave['event']!='execute-leave':raise ValueError('missing dispatch return')
  if scene in(2,4,6):
   if leave['unit']!=e['unit']:raise ValueError('canceled request dispatched')
  elif leave['unit']['head']!=[0xffffffff,0xffffffff]or leave['unit']['suppression']!=0:raise ValueError('work did not complete')
 # Observer tracks addresses: retain, but do not attribute this reused identity
 # from cleanup to the scene's Acolyte. All full records remain byte-pinned.
 return dict(captures=2,controls=1,public_markers=2382,native_boundaries=208,work_requests=16)
def validate(spec):
 if(spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_tests']!=TESTS or len(spec['instructions'])!=561):raise ValueError('Build contract changed')
 for n,h in spec['source_sha256'].items():
  if digest(ROOT/n)!=h:raise ValueError('source changed '+n)
 if digest(SPEC.with_suffix('.json.gz'))!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 captures=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()))
 if len(captures)!=3:raise ValueError('missing observed repeat/control')
 for i,c in enumerate(captures):
  rows=c['rows'];meta=rows[0];footer=rows[-1];pub=public(c['preload'])
  if(meta.get('task')!='ORDER-01.16'or meta.get('mode')!=('control'if i==2 else'observe')or not meta.get('owned')or meta.get('sha256')!=SHA or
     (meta.get('display'),meta.get('remote'))not in((':98','127.0.0.1:27049'),(':99','127.0.0.1:27050'))or meta['source_sha256']['map']!=spec['map_sha256']or
     footer.get('event')!='preload-file'or not footer.get('complete')or footer.get('markers')!=794 or
     hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']or pub!=spec['public']or len(pub)!=794 or
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
   if native(rows)!=spec['native']or len(spec['native'])!=104:raise ValueError('native Build ownership changed')
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
