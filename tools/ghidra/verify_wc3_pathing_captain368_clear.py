#!/usr/bin/env python3
"""Verify public ClearCaptainTargets callback order and retained request state."""
import argparse,gzip,json,copy
from pathlib import Path
from verify_wc3_pathing_captain358 import ROOT,SHA,digest,public,original_bytes
SPEC=ROOT/'tools/ghidra/fixtures/retail-captain368-clear-1.27.json'
TESTS=['wc3_bot.captain_clear368*']
INVALID=[0xffffffff,0xffffffff]
DIRECT_CALLERS=[0x9d0f59,0x9d0f7d,0x9d0f8c]

def projection(rows):
 """Preserve the complete synchronous Clear callback stream, including reissues.

 Void callback EAX values are undefined. Process pointers retain equality by
 encounter identity; the sole pointer stored inside current[] is the guarded
 FloatMini vtable at module+a9de14. Other integer words remain untouched.
 """
 ends=[i for i,r in enumerate(rows)if r.get('name')=='clear-targets']
 if len(ends)!=1:raise ValueError('missing unique Clear boundary')
 end=ends[0];counter=rows[end]['counter']
 calls=[i for i,r in enumerate(rows[:end])if r.get('name')=='member-removed'and r.get('counter')==counter and r.get('caller')in DIRECT_CALLERS]
 if len(calls)!=3 or [rows[i]['caller']for i in calls]!=DIRECT_CALLERS:raise ValueError('logical roles processed out of order')
 base=int(next(r['base']for r in rows if r.get('event')=='module'),16)
 aliases={};captains=set()
 def scalar(value):
  if isinstance(value,str)and value.startswith('0x'):
   if int(value,16)==0:return 'null'
   if value not in aliases:aliases[value]='identity'+str(len(aliases)+1)
   return aliases[value]
  if isinstance(value,list):return [scalar(v)for v in value]
  if isinstance(value,dict):
   result={k:scalar(v)for k,v in value.items()if k not in('ms',)}
   if 'current'in value:
    current=value['current'][:]
    if (current[1]-base)&0xffffffff!=0xa9de14:raise ValueError('unexpected embedded FloatMini vtable')
    current[1]=0xa9de14;result['current']=current
   return result
  return value
 # Anchor pointer identities in the already-observed creation/roster history.
 # First occurrence inside Clear alone would silently permit permuting members.
 def seed(value):
  if isinstance(value,str)and value.startswith('0x'):scalar(value)
  elif isinstance(value,list):
   for item in value:seed(item)
  elif isinstance(value,dict):
   for item in value.values():seed(item)
 for row in rows[:calls[0]]:seed(row)
 result=[]
 for row in rows[calls[0]:end+1]:
  r=copy.deepcopy(row)
  if r.get('counter')!=counter:raise ValueError('Clear callback crosses owner counter')
  if r.get('event')=='captain-call'and r.get('name')!='home-reevaluate':r.pop('result',None)
  if r.get('event')not in('captain-call','reissue','roster','engagement-test','captain-speed'):raise ValueError('unexpected Clear callback event')
  result.append(scalar(r))
 return result

def runtime(groups,spec):
 if set(groups)!=set(('point','follow')):raise ValueError('missing Clear scenes')
 for scene,count in [('point',233),('follow',451)]:
  cases=groups[scene];expected=spec['scenes'][scene]
  if len(cases)!=3 or len(expected['public'])!=count:raise ValueError('missing repeated/public control')
  for i,case in enumerate(cases):
   rows=case['rows'];meta=rows[0]
   if(meta.get('mode')!=('control'if i==2 else'observe')or not meta.get('owned')or meta.get('launcher')!='owned-wine-popen'or meta.get('sha256')!=SHA or
      meta.get('env')not in('B','C')or meta.get('display')not in(':98',':99')or meta.get('prefix')!='RSH 'or meta.get('input')or
      meta.get('source_sha256')!=expected['sources'][i]or meta['source_sha256']['map']!=expected['map_sha256']or
      any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('capture provenance/failure')
   if public(case['preload'])!=expected['public']:raise ValueError('public stream differs')
   if rows[-1]!={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':count,'complete':True}:raise ValueError('incomplete capture')
   if i==2:
    if any(r.get('event')in('module','trace-end','marker','snapshot','captain-call')for r in rows):raise ValueError('instrumented control')
    continue
   if [r['value']for r in rows if r.get('event')=='marker']!=expected['public']or not any(r.get('event')=='trace-end'and r.get('installed')for r in rows):raise ValueError('observer/public completion differs')
   if projection(rows)!=expected['projection']:raise ValueError('ordered Clear callback stream differs')
   r=next(r for r in rows if r.get('name')=='clear-targets');a,b=r['before'],r['after']
   if a['counts']!=[6,6,0,6,6,0]or b['counts']!=a['counts']or a['state']!=2 or b['state']!=2:raise ValueError('roster/state admission differs')
   if scene=='point':
    if a!=b or a['request']==a['home']or a['targets']!=[INVALID]*3:raise ValueError('targetless Clear replaced retained point')
   elif a['targets'][1]==INVALID or b['targets']!=[INVALID]*3 or a['request']!=b['request']or a['requestRange']!=b['requestRange']or a['flags']!=0x1001 or b['flags']!=0x3001:raise ValueError('follow Clear inverse differs')
 return dict(captures=4,controls=2,public_markers=684,clear_boundaries=4)

def validate(spec):
 if spec.get('task')!='GROUP-03.4.7.3'or spec.get('game_sha256')!=SHA or spec.get('engine_tests')!=TESTS or len(spec.get('instructions',{}))!=551:raise ValueError('wrong task/test/guard contract')
 for p,h in spec['pins'].items():
  if digest((ROOT/p).read_bytes())!=h:raise ValueError('source changed '+p)
 data=SPEC.with_suffix('.json.gz').read_bytes()
 if digest(data)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 groups=json.loads(gzip.decompress(data));runtime(groups,spec);return groups

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());groups=validate(spec);original_bytes(a.binary,spec['instructions'])
 from wc3_pathfinding_engine_tests import run_engine
 engine=run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=551,**runtime(groups,spec),engine=engine,scope=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
