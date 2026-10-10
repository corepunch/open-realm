#!/usr/bin/env python3
"""Verify canonical pending attachment ownership independently of physical rows."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work248-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_request248.h'
SOURCES=['tools/frida/research/work248_'+s for s in('observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/work228_capture.py','tools/ghidra/research/Work248Evidence.java',
 'tools/ghidra/research/work248_oracle.py','tools/ghidra/research/foot03_rig.py',
 'tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_movement.request248*','wc3_movement.selected*','wc3_movement.partition235*',
 'wc3_movement.queued*','wc3_movement.public_captain_thirteen_mixed_matches_original_complete_journey','wc3_order_lifecycle.*','wc3_interrupt.*',
 'wc3_save.rejects_prior_save_versions','wc3_save.rejects_layout_mismatch_before_selecting_map']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def header(original):
 out=['#ifndef RETAIL_REQUEST248_H','#define RETAIL_REQUEST248_H','/* Complete native readiness/drop; original fine counters, including pending rows. */','static struct {unsigned outer,drop;struct {unsigned attached,ready,counters[3];} stage[3][4];} const request248_rows[]={']
 for row in original['rows']:
  events=[]
  for e in row['events']:
   traces=[t for t in e['trace']if t['at']!='bind']
   if len(traces)!=4:raise ValueError('scope boundaries missing')
   events.append('{'+','.join('{%d,%d,{%s}}'%(sum(1<<i for i in t['attached']),sum(1<<i for i in t['ready']),','.join(map(str,t['counters'])))for t in traces)+'}')
  out.append('{%d,%d,{%s}},'%(row['outer'],row['drop'],','.join(events)))
 return '\n'.join(out+['};','#endif',''])
def normalized(rows):
 attaches=[r for r in rows if r.get('event')=='attach'];movers={tuple(r['mover']):i for i,r in enumerate(attaches)};units={r['unit']:i for i,r in enumerate(attaches)}
 retained=[r for r in rows if r.get('event')=='retain'];requests={tuple(r['request']):min(units[x['unit']]for x in retained if x['request']==r['request'])for r in retained}
 out=[]
 for r in rows:
  event=r.get('event');req=requests.get(tuple(r.get('request',())), -1)
  if event=='scope':out.append(dict(event=event,phase=r['phase'],request=req,rows=[dict(index=x['index'],unit=movers[tuple(x['id'])],ready=x['ready'],counter=x['counter'],bound=tuple(x['group'])!=(0xffffffff,0xffffffff))for x in r['rows']]))
  elif event=='bind':out.append(dict(event=event,request=req,index=r['index'],depth=r['depth'],flags=r['flags'],members=[movers[tuple(x)]for x in r['members']]))
  elif event=='ready':out.append(dict(event=event,request=req,unit=movers[tuple(r['mover'])]))
  elif event=='publish-ready':out.append(dict(event=event,request=req,flags=r['flags'],ready=[i for i,x in enumerate(r['slots'])if x!=0xffffffff]))
  elif event=='publish-ready-end':out.append(dict(event=event,result=r['result']))
  elif event=='marker'and'label=issued'in r['value']:out.append(dict(event='issued',unit=int(r['value'].rsplit('=',1)[1])))
 return out

def verify_scopes(events):
 scopes=[r for r in events if r['event']=='scope']
 if len(scopes)!=24:raise ValueError('expected six complete scopes')
 pending=0
 for i in range(0,len(scopes),4):
  group=scopes[i:i+4]
  if [r['phase']for r in group]!=['acquire-enter','acquire-leave','release-enter','release-leave']:raise ValueError('unbalanced scope')
  before,held,release,after=[r['rows']for r in group]
  if [r['unit']for r in before]!=[r['unit']for r in held]or[r['unit']for r in before]!=[r['unit']for r in after]:raise ValueError('lost attachment')
  if [r['counter']+1 for r in before]!=[r['counter']for r in held]or[r['counter']for r in held]!=[r['counter']for r in release]or[r['counter']for r in before]!=[r['counter']for r in after]:raise ValueError('counter not restored')
  if any(not r['ready']for r in held):
   pending+=1
   if [r['bound']for r in before]!=[r['bound']for r in after]:raise ValueError('pending member activated')
 if pending!=1:raise ValueError('missing busy pending witness')
 roots=[r for r in events if r['event']=='bind'and r['depth']==0]
 if [r['members']for r in roots]!=[[0],[1],[2],[3],[0]]:raise ValueError('publication order differs')
 if [r['result']for r in events if r['event']=='publish-ready-end']!=[1,0xffffffff,1,1,1,1]:raise ValueError('busy gate differs')
 if [r['unit']for r in events if r['event']=='issued']!=[0,3,1,2,0]:raise ValueError('issued callback boundaries differ')
 # Three idle callbacks precede their own readiness, while each prior class
 # is already published. This does not certify engine busy FIFO event timing.
 for unit in(3,1,2):
  issued=next(i for i,r in enumerate(events)if r==dict(event='issued',unit=unit))
  ready=next(i for i,r in enumerate(events)if r.get('event')=='ready'and r['unit']==unit)
  if issued>=ready:raise ValueError('ready before issued callback')
 return dict(scopes=6,pending_scopes=1)
def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=2:raise ValueError('two retail repeats required')
 for c in bundle['captures']:
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload'];ends=[r for r in rows if r.get('event')=='trace-end']
  markers=re.findall(r'call Preload\( "(P248 [^"\r\n]*)" \)',preload)
  if(meta['mode']!='observe'or meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or meta['source_sha256']['map']!=spec['map_sha256']or not footer.get('complete')or footer['markers']!=247 or len(markers)!=247 or markers!=[r['value']for r in rows if r.get('event')=='marker']or hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or len(ends)!=1 or not ends[0]['installed']or ends[0]['depth']or ends[0]['dispatch']or ends[0]['count']!=5 or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('incomplete retail capture')
  for p in(SOURCES[0],SOURCES[3]):
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('observer provenance differs')
  inputs=[r for r in rows if r.get('event')=='player-input']
  if len(inputs)!=1 or inputs[0]['rc']or not inputs[0]['plan']['alt']or not inputs[0]['plan']['shift']:raise ValueError('missing player input')
  events=normalized(rows);verify_scopes(events)
  if events!=spec['events']:raise ValueError('frozen chronology differs')
 meta=bundle['map_metadata']
 if(meta['sha256']!=spec['map_sha256']or meta['base_sha256']!='cccdb3ac84b2d965363af9c69659010f32fbb4124d94a70695597cf77a1bb162'or meta['probe_sha256']!=spec['pins'][SOURCES[2]]):raise ValueError('map provenance differs')
 return dict(captures=2,public_markers=494,scope_boundaries=48)
def native_controls(original,controls):
 def strip(s):return [dict(r,events=[{k:v for k,v in e.items()if k!='trace'}for e in r['events']])for r in s['rows']]
 if strip(original)!=strip(controls):raise ValueError('native observer affected results')
def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-04.6/MAP-04.2'or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS):raise ValueError('contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('changed source '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle changed')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,spec);native_controls(bundle['original'],bundle['controls'])
 if header(bundle['original'])!=HEADER.read_text():raise ValueError('literal original expectations changed')
 return bundle

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 s=json.loads(FIXTURE.read_text());bundle=validate(s);original_bytes(a.binary,s['instructions'])
 import sys
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import work248_oracle as native
 fresh=native.original(a.binary);control=native.original(a.binary,False);native_controls(fresh,control)
 if fresh!=bundle['original']or control!=bundle['controls']:raise ValueError('fresh native behavior differs')
 import verify_wc3_pathing_work242 as prior
 before=prior.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:prior.TESTS=TESTS;engine=prior.run_engine(a.test_binary,a.data,a.report)
 finally:prior.TESTS=before
 result=dict(passed=True,task=s['task'],**validate_runtime(bundle,s),original_cases=len(fresh['rows']),original_controls=len(control['rows']),instructions=len(s['instructions']),engine=engine,fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
