#!/usr/bin/env python3
"""Verify public mana-roster Captain contact and production save/inverse regression."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-captain358-1.27.json'
TESTS=['wc3_bot.captain358*','wc3_bot.captain_occupied_home*','wc3_bot.captain_attack_arrival*']
def digest(data):return hashlib.sha256(data).hexdigest()
def public(text):return re.findall(r'call Preload\( "(RSH [^"\r\n]*)" \)',text)
def contact(rows):
 result=[]
 for r in rows:
  if r.get('event')!='captain-call' or r.get('name')!='combat-notify':continue
  result.append({k:{n:r[k][n]for n in ('state','flags','order','counts','request','requestRange','home','targets')} for k in ('before','after')})
 return result
def runtime(cases,spec):
 if len(cases)!=3:raise ValueError('missing repeat/control')
 for i,case in enumerate(cases):
  rows=case['rows'];meta=rows[0];mode='control'if i==2 else'observe'
  if(meta.get('mode')!=mode or not meta.get('owned')or meta.get('launcher')!='owned-wine-popen' or
     meta.get('sha256')!=SHA or meta.get('env')not in ('B','C')or meta.get('display')not in(':98',':99')or
     meta.get('source_sha256')!=spec['capture_sources'][i]or meta['source_sha256']['map']!=spec['map_sha256']or
     meta.get('prefix')!='RSH 'or meta.get('input')or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):
   raise ValueError('capture ownership/provenance/failure')
  if public(case['preload'])!=spec['public']or len(spec['public'])!=439:raise ValueError('public control mismatch')
  expected={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':439,'complete':True}
  if rows[-1]!=expected:raise ValueError('incomplete public footer')
  if i==2:
   if any(r.get('event')in('module','trace-end','marker','snapshot','captain-call')for r in rows):raise ValueError('instrumented control')
   continue
  if [r['value']for r in rows if r.get('event')=='marker']!=spec['public']:raise ValueError('observed/public stream mismatch')
  if not any(r.get('event')=='trace-end'and r.get('installed')for r in rows):raise ValueError('unfinished observer')
  if contact(rows)!=spec['contact']or len(spec['contact'])!=1:raise ValueError('contact boundary differs')
  before,after=spec['contact'][0]['before'],spec['contact'][0]['after']
  if(before['counts']!=[6,6,0,6,6,0]or before['flags']!=0x1001 or after['flags']!=0x1001 or
     before['request']==before['home']or after['request']!=after['home']or
     before['requestRange']!=0x43480000 or after['requestRange']!=0x43fa0000 or
     before['order']!=0xd0014 or after['order']!=0xd0012):raise ValueError('mana-only return contract differs')
  boundary=next(r['counter']for r in rows if r.get('event')=='captain-call'and r.get('name')=='combat-notify')
  home=[r for r in rows if r.get('event')=='captain-call'and r.get('counter')==boundary and r.get('name')=='go-home']
  points=[r for r in rows if r.get('event')=='captain-call'and r.get('counter')==boundary and r.get('name')=='point-request']
  if len(home)!=1 or len(points)!=1 or points[0]['range']!=0x43fa0000 or points[0]['retain']!=1:raise ValueError('contact does not publish home500')
 return dict(captures=2,controls=1,public_markers=439,contact_boundaries=2)
def validate(spec):
 if spec.get('task')!='GROUP-03.4.7.3'or spec.get('game_sha256')!=SHA or spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=661:raise ValueError('wrong task/test contract')
 for p,h in spec['pins'].items():
  if digest((ROOT/p).read_bytes())!=h:raise ValueError('source changed '+p)
 bundle=SPEC.with_suffix('.json.gz').read_bytes()
 if digest(bundle)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 cases=json.loads(gzip.decompress(bundle));runtime(cases,spec);return cases
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());cases=validate(spec);original_bytes(a.binary,spec['instructions'])
 import wc3_pathfinding_engine_tests as runner
 engine=runner.run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=len(spec['instructions']),**runtime(cases,spec),engine=engine,scope=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
