#!/usr/bin/env python3
"""Verify independent Captain selection scheduling and ordinary target publication."""
import argparse,copy,gzip,json
from pathlib import Path
from verify_wc3_pathing_captain358 import ROOT,SHA,digest,public,original_bytes
SPEC=ROOT/'tools/ghidra/fixtures/retail-captain369-selection-1.27.json'
TESTS=['wc3_bot.captain_selection369*']
COUNTS={'clear':281,'reachable':282,'candidates':331,'transfer':332,'loss':307}

def projection(rows):
 base=int(next(r['base']for r in rows if r.get('event')=='module'),16)
 aliases={}
 def normalize(value):
  if isinstance(value,str)and value.startswith('0x'):
   if int(value,16)==0:return 'null'
   if value not in aliases:aliases[value]='identity'+str(len(aliases)+1)
   return aliases[value]
  if isinstance(value,list):return [normalize(x)for x in value]
  if isinstance(value,dict):
   result={k:normalize(v)for k,v in value.items()if k!='ms'}
   if 'current'in value:
    words=value['current'][:]
    if (words[1]-base)&0xffffffff!=0xa9de14:raise ValueError('unexpected FloatMini vtable')
    words[1]=0xa9de14;result['current']=words
   return result
  return value
 result=[]
 for row in rows:
  if row.get('event')=='captain-call'and row.get('name')in('begin-selection','selection-poll','clear-targets'):
   r=copy.deepcopy(row);r.pop('result',None);result.append(normalize(r))
  elif row.get('event')in('selection-candidate','selection-random'):result.append(normalize(row))
 return result

def runtime(groups,spec):
 if set(groups)!=set(COUNTS):raise ValueError('missing scenes')
 for scene,count in COUNTS.items():
  expected=spec['scenes'][scene];cases=groups[scene]
  if len(cases)!=3 or len(expected['public'])!=count:raise ValueError('missing repeat/control')
  for i,case in enumerate(cases):
   rows=case['rows'];meta=rows[0]
   if(meta.get('mode')!=('control'if i==2 else'observe')or meta.get('sha256')!=SHA or not meta.get('owned')or
      meta.get('launcher')!='owned-wine-popen'or meta.get('env')not in('B','C')or meta.get('display')not in(':98',':99')or
      meta.get('prefix')!='RSH 'or meta.get('input')or meta.get('source_sha256')!=expected['sources'][i]or
      any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('capture provenance/failure')
   if public(case['preload'])!=expected['public']:raise ValueError('public stream differs')
   if rows[-1]!={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':count,'complete':True}:raise ValueError('incomplete capture')
   if i==2:
    if any(r.get('event')in('module','trace-end','marker','captain-call')for r in rows):raise ValueError('instrumented control')
    continue
   if [r['value']for r in rows if r.get('event')=='marker']!=expected['public']or not any(r.get('event')=='trace-end'and r.get('installed')for r in rows):raise ValueError('missing observed completion')
   if projection(rows)!=expected['projection']:raise ValueError('selection stream differs '+scene)
   begin=[r for r in rows if r.get('name')=='begin-selection'];poll=[r for r in rows if r.get('name')=='selection-poll']
   if len(begin)!=(2 if scene=='loss'else 1)or any(not r['after']['selection']['active']or r['before']['periodic']!=r['after']['periodic']for r in begin):raise ValueError('owners conflated')
   if begin[0]['after']['selection']['request']['period']!=0x3dcccccd:raise ValueError('wrong native period')
   if scene=='clear':
    if poll:raise ValueError('canceled selection fired')
   elif scene=='loss':
    if len(poll)!=2 or begin[1]['caller']!=0x9d27b1 or begin[1]['after']['state']!=3 or not(begin[1]['after']['flags']&0x2000)or poll[-1]['after']['selection']['active']:raise ValueError('primary loss did not restore/rearm')
   else:
    if len(poll)!=(1 if scene=='reachable'else 7)or poll[-1]['after']['state']!=3 or poll[-1]['after']['selection']['active']:raise ValueError('eligible budget/publication differs')
 return dict(captures=10,controls=5,public_markers=sum(COUNTS.values()))

def validate(spec):
 if spec.get('task')!='GROUP-03.4.7.3'or spec.get('game_sha256')!=SHA or spec.get('engine_tests')!=TESTS:raise ValueError('wrong contract')
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
 report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=len(spec['instructions']),**runtime(groups,spec),engine=engine,scope=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
