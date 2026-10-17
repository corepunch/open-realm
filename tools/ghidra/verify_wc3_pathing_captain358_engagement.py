#!/usr/bin/env python3
"""Verify Captain policy0 contact and signed roster-delta repeats/control."""
import argparse,gzip,json
from pathlib import Path
from verify_wc3_pathing_captain358 import ROOT,SHA,digest,public,original_bytes
SPEC=ROOT/'tools/ghidra/fixtures/retail-captain358-engagement-1.27.json'
TESTS=['wc3_bot.captain_near_home_hostile_contact*','wc3_bot.captain_contact_preserves_negative*','wc3_save.rejects_version_201_before_captain_target_layout']
FIELDS=('state','flags','order','counts','request','requestRange','home','targets')
def boundaries(rows,name="combat-notify"):
 return [dict(counter=r['counter'],before={k:r['before'][k]for k in FIELDS},after={k:r['after'][k]for k in FIELDS})for r in rows if r.get('event')=='captain-call'and r.get('name')==name]
def runtime(groups,spec):
 if set(groups)!=set(('near','negative','lost')):raise ValueError('missing public scenes')
 for name,number in [('near',378),('negative',353),('lost',380)]:
  cases=groups[name];expected=spec['scenes'][name]
  if len(cases)!=3 or len(expected['public'])!=number:raise ValueError('missing repeat/control')
  for i,case in enumerate(cases):
   rows=case['rows'];meta=rows[0];mode='control'if i==2 else'observe'
   if(meta.get('mode')!=mode or not meta.get('owned')or meta.get('launcher')!='owned-wine-popen'or meta.get('sha256')!=SHA or
      meta.get('env')not in('B','C')or meta.get('display')not in(':98',':99')or meta.get('prefix')!='RSH 'or meta.get('input')or
      meta.get('source_sha256')!=expected['sources'][i]or meta['source_sha256']['map']!=expected['map_sha256']or
      any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('capture provenance/failure')
   if public(case['preload'])!=expected['public']:raise ValueError('public stream differs')
   if rows[-1]!={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':number,'complete':True}:raise ValueError('partial capture')
   if i==2:
    if any(r.get('event')in('module','trace-end','marker','snapshot','captain-call')for r in rows):raise ValueError('instrumented control')
    continue
   if [r['value']for r in rows if r.get('event')=='marker']!=expected['public']:raise ValueError('observer/public disagreement')
   if not any(r.get('event')=='trace-end'and r.get('installed')for r in rows):raise ValueError('unfinished observer')
   contacts=boundaries(rows)
   if contacts!=expected['contacts']or len(contacts)!=1:raise ValueError('contact differs')
   c=contacts[0];a,b=c['before'],c['after']
   if a['flags']!=0x1001 or b['flags']!=0x1001 or a['requestRange']!=0x43480000:raise ValueError('contact admission differs')
   if name=='negative':
    if a['counts']!=[6,5,-1,5,5,0]or b['counts']!=a['counts']or a['request']==a['home']or b['request']!=b['home']or b['requestRange']!=0x43fa0000:raise ValueError('signed delta/home inverse differs')
   else:
    if a['counts']!=[6,6,0,6,6,0]or a['request']!=a['home']or b['request']!=a['request']or b['requestRange']!=a['requestRange']or b['targets'][1]==[0xffffffff,0xffffffff]:raise ValueError('policy0 retained request differs')
    refresh=[r for r in rows if r.get('name')=='refresh-target'and r.get('counter')==c['counter']]
    points=[r for r in rows if r.get('name')=='point-request'and r.get('counter')==c['counter']]
    if len(refresh)!=1 or refresh[0]['policy']!=0 or refresh[0]['range']!=0x43480000 or len(points)!=1 or points[0]['retain']!=0 or points[0]['prepare']!=1:raise ValueError('policy0 route publication differs')
   if name=='lost':
    losses=boundaries(rows,'member-removed')
    if losses!=expected['losses']or len(losses)!=1:raise ValueError('source-loss boundary differs')
    loss=losses[0];a,b=loss['before'],loss['after']
    if a['counts']!=[6,6,0,6,6,0]or b['counts']!=a['counts']or a['request']!=b['request']or b['requestRange']!=0x43480000 or b['flags']!=0x3001 or b['targets']!=[[0xffffffff,0xffffffff]]*3:raise ValueError('source loss did not restore retained request')
    refresh=[r for r in rows if r.get('name')=='refresh-target'and r.get('counter')==loss['counter']]
    if len(refresh)!=1 or refresh[0]['policy']!=1 or refresh[0]['range']!=0x43480000:raise ValueError('source-loss refresh differs')
 return dict(captures=6,controls=3,public_markers=1111,contact_boundaries=6,loss_boundaries=2)
def validate(spec):
 if spec.get('task')!='GROUP-03.4.7.3'or spec.get('game_sha256')!=SHA or spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=1158:raise ValueError('wrong task/guards/tests')
 for p,h in spec['pins'].items():
  if digest((ROOT/p).read_bytes())!=h:raise ValueError('source changed '+p)
 data=SPEC.with_suffix('.json.gz').read_bytes()
 if digest(data)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 groups=json.loads(gzip.decompress(data));runtime(groups,spec);return groups

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());groups=validate(spec);original_bytes(a.binary,spec['instructions'])
 import wc3_pathfinding_engine_tests as runner
 engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=1158,**runtime(groups,spec),engine=engine,scope=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
