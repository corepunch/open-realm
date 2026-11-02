#!/usr/bin/env python3
"""Check selected queued-point activation against repeated read-only retail traces."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work250-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/work250_'+s for s in('observer.js','probe.j','make_map.py')]+[
 'tools/frida/research/work250b_probe.j','tools/frida/research/work250b_make_map.py',
 'tools/frida/research/work228_capture.py','tools/ghidra/research/Work250Evidence.java']
TESTS=['wc3_movement.queued250*','wc3_movement.request248*','wc3_movement.queued249*',
 'wc3_movement.queued236*','wc3_movement.selected*','wc3_order_reentry.*',
 'wc3_order_lifecycle.*','wc3_interrupt.*','wc3_save.rejects_prior_save_versions',
 'wc3_model.*','wc3_support.*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def normalize(rows):
 units={};pending=None
 for r in rows:
  if r.get('event')=='issued-point-enter':pending=r['unit']
  if r.get('event')=='marker'and'label=issued 'in r['value']:
   m=re.search(r'unit=(\d+)',r['value'])
   if pending is None or m is None:raise ValueError('unobserved issued callback')
   index=int(m[1])
   if pending in units and units[pending]!=index:raise ValueError('identity changed')
   units[pending]=index;pending=None
 if sorted(units.values())!=[0,1,2,3]:raise ValueError('missing selected identities')
 out=[]
 events={prefix+suffix for prefix in('append','head-dispatch','issued-point')for suffix in('-enter','-leave')}|{'point-task','cohort-search'}
 for r in rows:
  event=r.get('event')
  if event in events:
   if r['unit']not in units:raise ValueError('unexpected order owner')
   s=r['state'];record=dict(event=event,tick=r['tick'],unit=units[r['unit']],
    head=s['head']!=[0xffffffff]*2,internal=s['internal']!=[0xffffffff]*2,
    count=s['count'],bound=s['group']!=[0xffffffff]*2)
   if 'command'in r:record['command']=r['command']
   out.append(record)
  elif event=='marker'and any('label='+label in r['value']for label in('issued ','redirect ','stop ')):
   out.append(dict(event='script',value=r['value']))
  elif event=='bind':out.append(dict(event='bind',tick=r['tick'],flags=r['flags'],count=len(r['members'])))
 return out

def verify_order(events,nested):
 issued=[r for r in events if r['event']=='script'and'label=issued 'in r['value']]
 if [int(re.search(r'unit=(\d+)',r['value'])[1])for r in issued]!=([0,3,3,1,2,0]if nested else[0,3,1,2,0]):raise ValueError('issued order differs')
 appends=[r for r in events if r['event']=='append-leave'and r['unit']==0 and r['tick']<=29]
 if len(appends)!=2 or appends[-1]['count']!=2 or not appends[-1]['internal']or not appends[-1]['bound']:raise ValueError('busy head not retained')
 busy=[r for r in events if r.get('tick')==29 and r.get('unit')==0]
 if [r['event']for r in busy]!=['append-enter','append-leave']:raise ValueError('busy enqueue executed its head')
 for i,r in enumerate(events):
  if r['event']!='issued-point-enter':continue
  if r['internal']or r['bound']or not r['head']:raise ValueError('task created before issued event')
  if events[i+1]['event']!='script':raise ValueError('missing callback boundary')
 binds=[r['flags']for r in events if r['event']=='bind']
 if binds!=([0,0,14,14,0]if nested else[0,14,14,14,0]):raise ValueError('replacement inherited prepared request')
 late=[r for r in events if r.get('tick')==81 and r.get('unit')==0]
 if len([r for r in late if r['event']=='point-task'])!=1 or len([r for r in late if r['event']=='cohort-search'])!=1:raise ValueError('activation missing task/query')
 if nested:
  task=next(r for r in late if r['event']=='point-task')
  if task['head']or task['count']or not task['internal']:raise ValueError('instant Stop incorrectly canceled outer task')
  idle=[r for r in events if r.get('tick')==29 and r.get('unit')==3]
  if len([r for r in idle if r['event']=='point-task'])!=1:raise ValueError('nested Move executed twice')
 else:
  if any(not r['head']for r in late):raise ValueError('normal head disappeared')
 return len(events)

def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=4:raise ValueError('two repeats per variant required')
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta=rows[0];footer=rows[-1];nested=i>=2
  markers=re.findall(r'call Preload\( "(P250 [^"\r\n]*)" \)',c['preload'])
  if(meta.get('mode')!='observe'or meta.get('sha256')!=SHA or not meta.get('owned')or meta.get('env')not in('B','C')or
   meta['source_sha256']['map']!=spec['maps'][int(nested)]or meta['input']!=spec['input']or
   footer.get('event')!='preload-file'or not footer.get('complete')or footer['markers']!=(250 if nested else 247)or
   len(markers)!=footer['markers']or markers!=[r['value']for r in rows if r.get('event')=='marker']or
   hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']):raise ValueError('incomplete/unowned capture')
  for path in(SOURCES[0],SOURCES[5]):
   if meta['source_sha256'][Path(path).name]!=spec['pins'][path]:raise ValueError('observer/runner differs')
  ends=[r for r in rows if r.get('event')=='trace-end'];inputs=[r for r in rows if r.get('event')=='player-input']
  if len(ends)!=1 or ends[0]!={'event':'trace-end','installed':True,'dispatch':0,'count':5,'depth':0}or len(inputs)!=1 or inputs[0]['rc']or inputs[0]['plan']!=spec['input'][0]or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows):raise ValueError('failed/incomplete instrumentation')
  actual=normalize(rows);verify_order(actual,nested)
  if actual!=spec['events'][int(nested)]:raise ValueError('frozen activation chronology differs')
 return dict(captures=4,public_markers=994,normal_events=len(spec['events'][0]),nested_events=len(spec['events'][1]))

def validate(spec):
 if spec['version']!=1 or spec['task']!='ORDER-02.2/GROUP-04.6'or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS:raise ValueError('contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source changed '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle changed')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,spec);return bundle

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(FIXTURE.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as runner
 saved=runner.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=saved
 result=dict(passed=True,task=spec['task'],**validate_runtime(bundle,spec),instructions=len(spec['instructions']),engine=engine,binary_sha256=SHA,fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
