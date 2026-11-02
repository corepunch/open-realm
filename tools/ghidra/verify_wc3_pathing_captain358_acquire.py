#!/usr/bin/env python3
"""Verify the independent AI-point Attack scan and saved Captain producer."""
import argparse,gzip,json
from pathlib import Path
from verify_wc3_pathing_captain358 import ROOT,SHA,digest,public,original_bytes,contact
SPEC=ROOT/'tools/ghidra/fixtures/retail-captain358-acquire-1.27.json'
TESTS=['wc3_bot.captain_ai_point_acquisition*','wc3_save.rejects_version_204_before_independent_attack_scan','wc3_movement.target168_pending_fade_rebases_without_changing_origin']
INVALID=[0xffffffff,0xffffffff]
def projection(rows):
 c=[r for r in rows if r.get('name')=='combat-notify']
 if len(c)!=1:raise ValueError('missing fresh Captain contact')
 counter=c[0]['counter']
 timers=[r for r in rows if r.get('event')=='acquire-timer'and r['counter']==counter]
 publishers=[r for r in rows if r.get('event')=='acquire-publish'and r['counter']==counter]
 if len(timers)!=1 or len(publishers)!=1:raise ValueError('missing acquisition owner')
 t,p=timers[0],publishers[0]
 if p['target']!=c[0]['source']or p['before']['unit']!=t['before']['unit']:raise ValueError('producer source differs')
 return dict(counter=counter,caller=c[0]['caller'],contact=contact(rows),timer={side:{k:t[side][k]for k in('state','attack')}for side in('before','after')},publish={side:{k:p[side][k]for k in('state','attack')}for side in('before','after')},publish_caller=p['caller'])
def runtime(cases,spec):
 if len(cases)!=3 or len(spec.get('public',[]))!=379:raise ValueError('missing repeats/control')
 for i,case in enumerate(cases):
  rows=case['rows'];meta=rows[0]
  if(meta.get('mode')!=('control'if i==2 else'observe')or not meta.get('owned')or meta.get('launcher')!='owned-wine-popen'or meta.get('sha256')!=SHA or
     meta.get('env')not in('B','C')or meta.get('display')not in(':98',':99')or meta.get('prefix')!='RSH 'or meta.get('input')or
     meta.get('source_sha256')!=spec['sources'][i]or meta['source_sha256']['map']!=spec['map_sha256']or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('capture provenance/failure')
  if public(case['preload'])!=spec['public']:raise ValueError('public stream differs')
  if rows[-1]!={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':379,'complete':True}:raise ValueError('partial capture')
  if i==2:
   if any(r.get('event')in('module','trace-end','marker','snapshot','captain-call','acquire-timer')for r in rows):raise ValueError('instrumented control')
   continue
  if [r['value']for r in rows if r.get('event')=='marker']!=spec['public']or not any(r.get('event')=='trace-end'and r.get('installed')for r in rows):raise ValueError('observer/public completion differs')
  delays=[r for r in rows if r.get('event')=='acquisition-delays']
  if len(delays)!=1 or delays[0]['idle']!=0x3f000000 or delays[0]['active']!=0x3f800000:raise ValueError('scan periods differ')
  p=projection(rows)
  if p!=spec['projection']or p['caller']!=0x4941a0 or p['publish_caller']!=0x494103:raise ValueError('fresh acquisition boundary differs')
  if(p['timer']!={'before':{'state':7,'attack':INVALID},'after':{'state':7,'attack':INVALID}}or
     p['publish']['before']!={'state':7,'attack':INVALID}or p['publish']['after']['state']!=1 or p['publish']['after']['attack']==INVALID):raise ValueError('publication/reissue order differs')
  a,b=p['contact'][0]['before'],p['contact'][0]['after']
  if a['counts']!=[6,6,0,6,6,0]or b['counts']!=a['counts']or a['flags']!=0x1001 or b['flags']!=0x1001 or a['request']==a['home']or b['request']!=b['home']or a['requestRange']!=0x43480000 or b['requestRange']!=0x43fa0000:raise ValueError('fresh scan did not return Captain home500')
  counter=p['counter'];selected=[]
  for event,name in [('acquire-publish',None),('captain-call','go-home'),('captain-call','combat-notify'),('acquire-timer',None)]:
   indices=[n for n,r in enumerate(rows)if r.get('event')==event and(name is None or r.get('name')==name)and r.get('counter')==counter]
   if len(indices)!=1:raise ValueError('missing ordered scan publication')
   selected.append(indices[0])
  if selected!=sorted(selected):raise ValueError('callback order differs')
 return dict(captures=2,controls=1,public_markers=379,fresh_scan_boundaries=2)
def validate(spec):
 if spec.get('task')!='GROUP-03.4.7.3'or spec.get('game_sha256')!=SHA or spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=1300:raise ValueError('wrong task/guards/tests')
 for p,h in spec['pins'].items():
  if digest((ROOT/p).read_bytes())!=h:raise ValueError('source changed '+p)
 data=SPEC.with_suffix('.json.gz').read_bytes()
 if digest(data)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 cases=json.loads(gzip.decompress(data));runtime(cases,spec);return cases

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());cases=validate(spec);original_bytes(a.binary,spec['instructions'])
 import wc3_pathfinding_engine_tests as runner
 engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=1300,**runtime(cases,spec),engine=engine,scope=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
