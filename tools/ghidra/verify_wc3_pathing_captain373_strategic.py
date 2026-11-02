#!/usr/bin/env python3
"""Verify native strategic graph inputs and actual engine publication-boundary parity."""
import argparse,gzip,hashlib,json,sys
from pathlib import Path
from verify_wc3_pathing_captain358 import ROOT,SHA,public,original_bytes
sys.path.insert(0,str(Path(__file__).resolve().parent/'research'))
SPEC=ROOT/'tools/ghidra/fixtures/retail-captain373-strategic-1.27.json'
TESTS=['wc3_bot.captain_strategic373*','wc3_bot.captain_regions373*']
def digest(data):return hashlib.sha256(data).hexdigest()
def semantic(rows):
 built=next(r for r in rows if r.get('event')=='region-built');filtered=next(r for r in rows if r.get('event')=='region-filtered')
 return dict(width=built['inputWords'][0],height=built['inputWords'][1],cells=built['cells'],scale=built['state']['scale'],sources=[{k:r[k]for k in ('type','words','counter')}for r in rows if r.get('event')=='region-source-position'],built=built['state'],filtered={k:filtered[k]for k in ('selected','state','counter')},point_before=next(r for r in rows if r.get('event')=='region-consumer-begin')['point'],draws=[{k:r[k]for k in ('before','after','result','counter')}for r in rows if r.get('event')=='purpose13-state'],point_after=next(r for r in rows if r.get('event')=='region-random-point')['point'])
def projection(rows,scene):
 if scene=='regions':return semantic(rows)
 if scene=='sources':return dict(kernel=semantic(rows),sources=[r['rawcode']for r in rows if r.get('event')=='region-owner-structure'],retirement=[{k:v for k,v in r.items()if k!='ms'}for r in rows if r.get('event')=='source-world-retired'])
 result=[]
 for r in rows:
  selected=r.get('event')in('source-world-retired','source-world-shown','source-target-lost')or(r.get('event')=='selection-candidate'and r.get('rawcode')==0x4870616c)
  if selected:result.append({k:v for k,v in r.items()if k not in('ms','unit','captain','identity','mover')})
 return result

def runtime(spec,bundle,oracle):
 if set(bundle)!=set(('regions','sources','coexistence','notification')):raise ValueError('missing native scene')
 for scene,cases in bundle.items():
  expected=spec['scenes'][scene]['public']
  if len(cases)!=(1 if scene=='notification'else 3):raise ValueError('missing repeat/control '+scene)
  for index,case in enumerate(cases):
   rows=case['rows'];meta=rows[0];control=index==2
   if meta.get('sha256')!=SHA or meta.get('mode')!=('control'if control else'observe')or not meta.get('owned')or meta.get('launcher')!='owned-wine-popen'or meta.get('env')not in('B','C')or meta.get('display')not in(':98',':99')or meta.get('prefix')!='RSH 'or meta.get('input')or meta.get('source_sha256')!=spec['runtime_sources'][scene][index]:raise ValueError('wrong capture provenance')
   if any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows):raise ValueError('runtime error')
   if public(case['preload'])!=expected:raise ValueError('public stream differs')
   if rows[-1]!={'event':'preload-file','sha256':digest(case['preload'].encode()),'markers':len(expected),'complete':True}:raise ValueError('incomplete capture')
   if control:
    if any(r.get('event')in('module','trace-end','marker','captain-call')for r in rows):raise ValueError('control instrumented')
   else:
    if [r['value']for r in rows if r.get('event')=='marker']!=expected or not any(r.get('event')=='trace-end'and r.get('installed')for r in rows):raise ValueError('missing observer completion')
    if projection(rows,scene)!=spec['scenes'][scene]['projection']:raise ValueError('native projection differs '+scene)
    if scene=='regions'and semantic(rows)!=oracle['case']:raise ValueError('portable native input differs')
 if spec['scenes']['regions']['public']!=oracle['public_markers']or[len(spec['scenes'][x]['public'])for x in('regions','sources','coexistence')]!=[331,332,387]or spec['scenes']['notification']['public']!=spec['scenes']['coexistence']['public']:raise ValueError('wrong public contract')
 case=oracle['case']
 if case['built']['count']!=67 or len(case['built']['grid'])!=65536 or len(case['built']['regions'])!=1072 or case['filtered']['state']['eligible']!=[3,8]or case['filtered']['state']['near']!=[3,8]or case['point_after']!=[3306160128,1132462080]:raise ValueError('native region contract changed')
 coex=spec['scenes']['coexistence']['projection'];queries=[r for r in coex if r['event']=='selection-candidate'];retired=next(r for r in coex if r['event']=='source-world-retired');shown=next(r for r in coex if r['event']=='source-world-shown')
 if len(queries)!=2 or any(r['invisibilityDepth']!=1 or r['flags5c']&0x118 for r in queries)or retired['after']['flags']!=retired['before']['flags']|8 or not(retired['after']['widget']&1)or shown['after']['flags']&8 or shown['before']['invisibilityDepth']!=1 or shown['after']['invisibilityDepth']!=1 or not(queries[0]['counter']<retired['counter']<shown['counter']<queries[1]['counter']):raise ValueError('world and invisibility owners conflated')
 lost=[r for r in spec['scenes']['notification']['projection']if r['event']=='source-target-lost']
 if len(lost)!=2 or lost[0]['caller']!=0x68b7d8 or lost[0]['flags']&8 or lost[1]['caller']!=0x688373 or not(lost[1]['flags']&8)or not(lost[1]['widget']&1)or any(r['invisibilityDepth']!=1 for r in lost):raise ValueError('wrong target-loss chronology')
 return dict(captures=7,controls=3,public_markers=1050,input_words=len(case['cells']),grid_bytes=65536,node_words=1072,random_draws=len(case['draws']))
def validate(spec):
 if spec.get('task')!='GROUP-03.4.7.3'or spec.get('game_sha256')!=SHA or spec.get('engine_tests')!=TESTS:raise ValueError('wrong contract')
 for path,sha in spec['pins'].items():
  if digest((ROOT/path).read_bytes())!=sha:raise ValueError('source changed '+path)
 oracle_bytes=(ROOT/spec['oracle']).read_bytes();bundle_bytes=SPEC.with_suffix('.json.gz').read_bytes()
 if digest(oracle_bytes)!=spec['oracle_sha256']or digest(bundle_bytes)!=spec['bundle_sha256']:raise ValueError('frozen input changed')
 oracle=json.loads(gzip.decompress(oracle_bytes));bundle=json.loads(gzip.decompress(bundle_bytes));runtime(spec,bundle,oracle);return bundle,oracle

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());bundle,oracle=validate(spec);guards=original_bytes(a.binary,spec['instructions'])
 from wc3_pathfinding_engine_tests import run_engine
 engine=run_engine(a.test_binary,a.data,a.report,TESTS,timeout=300)
 if engine!={'classic':{'tests':6,'assertions':139},'tft':{'tests':6,'assertions':139}}:raise ValueError('engine regression count changed')
 report=dict(passed=True,status='verified',binary_sha256=SHA,instructions=guards,**runtime(spec,bundle,oracle),engine=engine,scope=spec['scope']);a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
