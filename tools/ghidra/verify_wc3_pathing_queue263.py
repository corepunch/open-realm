#!/usr/bin/env python3
"""Verify user queue activation, rejected successors and cancellation in original retail.

Expected script streams come only from repeated original captures, including
observer-free controls. Original canonical retirement is distinct from payload
zero-reference reclamation; retained UI request references are recorded.
"""
from retail_capture_source import matches_capture_source
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-queue263-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/queue263_'+s for s in('capture.py','observer.js','make_map.py','probe.j','ui_input.c')]+[
 'tools/frida/research/order0110_ui_input.c','tools/frida/research/group032_make_map.py',
 'tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/Queue263Evidence.java']
TESTS=['wc3_order_lifecycle.*','wc3_order_reentry.*','wc3_interrupt.*','wc3_movement.queued250*',
 'wc3_movement.target220*','wc3_movement.target251*','wc3_movement.queued249*',
 'wc3_save.rejects_prior_save_versions','wc3_e2e213.queue_and_shared_pool_pressure_keep_saved_payloads_and_reuse']
VARIANTS=('removed','repair','cancel','empty','alive')
INVALID=[0xffffffff,0xffffffff]
MOVE,ATTACK,REPAIR,STOP=851986,851983,852024,851972
EXPECTED={
 'removed':[(MOVE,'point-event'),(ATTACK,'point-event'),(MOVE,'point-event'),(STOP,'immediate-event')],
 'repair':[(MOVE,'point-event'),(REPAIR,'point-event'),(MOVE,'point-event'),(STOP,'immediate-event')],
 'cancel':[(MOVE,'point-event'),(STOP,'immediate-event'),(STOP,'immediate-event')],
 'empty':[(MOVE,'point-event'),(STOP,'immediate-event')],
 'alive':[(MOVE,'point-event'),(ATTACK,'target-event'),(STOP,'immediate-event')]}
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(Q263 [^"\r\n]*)" \)',preload)
def native(rows,variant):
 appends=[r for r in rows if r.get('event')=='append']
 expected_commands=[MOVE]+([]if variant=='empty'else[REPAIR if variant=='repair'else ATTACK,MOVE])+[STOP]
 if variant=='cancel':expected_commands.insert(-1,STOP)
 if [r['order']['command']for r in appends]!=expected_commands:raise ValueError('missing genuine queued admission')
 ids=[r['order']['identity']for r in appends]
 if len({tuple(x)for x in ids})!=len(ids):raise ValueError('reused user identity')
 def packet_state(r):
  s=r['state'];return dict(count=s['count'],head=ids.index(s['user'])if s['user']in ids else None,
    tail=ids.index(s['tail'])if s['tail']in ids else None,internal=s['tasks']!=INVALID)
 if appends[0]['state']['count']!=0 or appends[0]['state']['user']!=INVALID or appends[0]['state']['tail']!=INVALID:
  raise ValueError('initial queue is not empty')
 if variant!='empty':
  for i in(1,2):
   a=appends[i];s=a['state'];at=rows.index(a)
   end=next((r for r in rows[at+1:]if r.get('event')=='append-end'),None)
   if not end or a['order']['flags']&4==0 or not 30<=a['tick']<80 or s['count']!=i or s['user']!=ids[0]or s['tasks']==INVALID:
    raise ValueError('Shift append did not preserve executing head/task')
   es=end['state']
   if es['count']!=i+1 or es['user']!=s['user']or es['tasks']!=s['tasks']or es['tail']!=ids[i]:raise ValueError('append head/tail/count differs')
   if any(r.get('event')in('dispatch','point-event','target-event','immediate-event')for r in rows[at+1:rows.index(end)]):
    raise ValueError('queued append notified or executed prematurely')
 events=[r for r in rows if r.get('event')in('point-event','target-event','immediate-event')]
 if [(r['order']['command'],r['event'])for r in events]!=EXPECTED[variant]:raise ValueError('activation event shape differs')
 for r in events:
  if r['state']['tasks']!=INVALID or r['state']['user']!=r['order']['identity']:raise ValueError('notification did not precede task')
  script=next((x for x in rows[rows.index(r)+1:]if x.get('event')=='marker'),None)
  if not script or f'label=issued event={r["order"]["command"]} head={r["order"]["command"]}'not in script['value']:
   raise ValueError('issued callback did not observe new public head')
 if variant in('removed','repair','alive'):
  e=events[1]
  if e['order']['identity']!=ids[1]or e['order']['target']==INVALID or e['order']['point']!=appends[1]['order']['point']:
   raise ValueError('queued target packet lost its retained point/identity')
  if variant!='alive' and e['event']!='point-event':raise ValueError('unresolved target did not publish point')
  if variant=='alive' and 'target=2 'not in next(x['value']for x in rows[rows.index(e)+1:]if x.get('event')=='marker'):
   raise ValueError('live target payload missing')
 dispatches=[r for r in rows if r.get('event')=='dispatch']
 if [r['order']['command']for r in dispatches]!=[x[0]for x in EXPECTED[variant]]:raise ValueError('dispatch and issued events differ')
 if variant=='repair':
  d=[r for r in dispatches if r['order']['identity']in(ids[1],ids[2])]
  if len(d)!=2 or [r['depth']for r in d]!=[1,2]or [r['state']['count']for r in d]!=[2,1]:raise ValueError('rejected Repair did not synchronously dispatch successor')
  scripts=[r['value']for r in rows if r.get('event')=='marker'and'label=issued 'in r['value']]
  if not scripts[1].startswith('Q263 tick=87 ')or not scripts[2].startswith('Q263 tick=87 '):raise ValueError('failed successor changed the activation clock')
 if variant=='cancel':
  cancels=[r for r in rows if r.get('event')=='cancel'and r['state']['count']==3]
  if len(cancels)!=1:raise ValueError('missing pending cancellation')
  c=cancels[0];end=next(r for r in rows[rows.index(c)+1:]if r.get('event')=='cancel-end')
  if c['state']['user']!=ids[0]or end['state']['count']!=1 or end['state']['tail']!=ids[0]or end['state']['user']!=ids[0]:raise ValueError('cancellation did not preserve head')
 release=[r for r in rows if r.get('event')=='release'];ends=[r for r in rows if r.get('event')=='release-end']
 requests=[r for r in rows if r.get('event')=='release-request']
 if len(release)!=len(ids)or len(ends)!=len(ids)or len(requests)!=len(ids):raise ValueError('missing original deferred release')
 if {tuple(r['identity'])for r in release}!={tuple(x)for x in ids}:raise ValueError('release identities differ')
 for r,e in zip(release,ends):
  if r['identity']!=e['identity']or e['resolved']or r['references']!=e['references']+1:raise ValueError('canonical/reference release differs')
 final=[r['state']for r in rows if r.get('event')=='state'][-1]
 if final['count']or final['user']!=INVALID or final['tail']!=INVALID or final['tasks']!=INVALID:raise ValueError('final queues did not retire')
 return dict(admissions=len(ids),events=[dict(command=r['order']['command'],kind=r['event'],state=packet_state(r))for r in events],
  releases=[dict(index=ids.index(r['identity']),before=r['references'],after=e['references'])for r,e in zip(release,ends)])
def runtime(bundle,spec):
 if len(bundle['captures'])!=15:raise ValueError('five repeated cases and unhooked controls required')
 summaries={};public=0
 for index,c in enumerate(bundle['captures']):
  variant=VARIANTS[index//3];mode='control'if index%3==2 else'observe';rows=c['rows'];meta,footer=rows[0],rows[-1]
  values=markers(c['preload']);public+=len(values)
  if(c['variant']!=variant or meta.get('mode')!=mode or meta.get('sha256')!=SHA or meta.get('task')!='ORDER-02.3'or
    not meta.get('owned')or meta.get('env')not in('B','C')or meta['source_sha256']['map']!=spec['maps'][variant]or
    meta['display']!={'B':':98','C':':99'}[meta['env']]or meta['remote']!={'B':'127.0.0.1:27049','C':'127.0.0.1:27050'}[meta['env']]or
    footer.get('event')!='preload-file'or not footer.get('complete')or footer['markers']!=len(values)or
    hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']or values!=spec['public'][variant]):raise ValueError('changed, incomplete or unowned capture')
  if mode=='observe'and values!=[r['value']for r in rows if r.get('event')=='marker'and'label=start 'not in r['value']]:raise ValueError('observer/public stream differs')
  for p in SOURCES[:6]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs')
  for p,h in spec['helper_sha256'].items():
   if meta['source_sha256'][p]!=h:raise ValueError('native input helper differs')
  inputs=[r for r in rows if r.get('event')=='player-input']
  if len(inputs)!=(0 if variant=='empty'else 2)or any(r['rc']for r in inputs)or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows):raise ValueError('input or instrumentation failed')
  if mode=='observe':
   end=[r for r in rows if r.get('event')=='trace-end']
   if len(end)!=1 or not end[0]['installed']or end[0]['depth']:raise ValueError('incomplete dispatcher unwind')
   result=native(rows,variant)
   if result!=spec['native'][variant]:raise ValueError('original queue/reference result differs')
   summaries[variant]=result
  elif any(r.get('event')in('module','dispatch','append')for r in rows):raise ValueError('control has instrumentation')
 return dict(captures=15,public_markers=public,native_scenarios=10,admissions=sum(x['admissions']for x in summaries.values())*2,
  activations=sum(len(x['events'])for x in summaries.values())*2)
def validate(spec):
 if(spec['version']!=1 or spec['task']!='ORDER-02.3'or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or
   spec['engine_tests']!=TESTS or set(spec['maps'])!=set(VARIANTS)or len(spec['instructions'])!=spec['instruction_count']):raise ValueError('queue contract differs')
 for p,h in spec['pins'].items():
  if not matches_capture_source(ROOT,p,h):raise ValueError('source changed '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 return spec

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=validate(json.loads(FIXTURE.read_text()));bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));live=runtime(bundle,spec);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as runner
 saved=runner.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=saved
 result=dict(passed=True,task=spec['task'],**live,instructions=len(spec['instructions']),engine=engine,
  binary_sha256=SHA,fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),test_binary_sha256=digest(a.test_binary),
  game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
