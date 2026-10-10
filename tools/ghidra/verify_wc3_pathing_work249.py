#!/usr/bin/env python3
"""Verify queued canonical scopes and inherited owner lifetime against retail."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work249-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_queued_scope249.h'
SOURCES=['tools/frida/research/work249_observer.js','tools/frida/research/work228_capture.py',
 'tools/ghidra/research/Work249Evidence.java','tools/ghidra/research/work249_oracle.py',
 'tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py',
 'tools/ghidra/verify_wc3_pathing_work236.py','tools/ghidra/verify_wc3_pathing_work242.py',
 'tools/ghidra/verify_wc3_pathing_target_normalize218.py']
TESTS=['wc3_movement.queued249*','wc3_movement.request248*','wc3_movement.cohort232*',
 'wc3_movement.queued*','wc3_movement.partition235*','wc3_movement.selected*',
 'wc3_movement.public_captain_thirteen_mixed_matches_original_complete_journey',
 'wc3_order_lifecycle.*','wc3_interrupt.*','wc3_save.rejects_prior_save_versions',
 'wc3_save.rejects_layout_mismatch_before_selecting_map','wc3_save.rejects_invalid_physical_group_payloads']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def header(original):
 rows=original['rows'];trace=rows[0]['trace']
 if len(rows)!=48 or any(r['trace']!=trace for r in rows):raise ValueError('native scope matrix differs')
 if [t['at']for t in trace]!=['acquire-enter','acquire-leave','release-enter','release-leave']*3:raise ValueError('native scopes incomplete')
 out=['#ifndef RETAIL_QUEUED_SCOPE249_H','#define RETAIL_QUEUED_SCOPE249_H',
 '/* All48 original5faaf0 cases share these twelve exact scope boundaries. */',
 'static struct {unsigned attached,ready,counters[3],inherited;} const queued249_scopes[]={']
 for t in trace:
  out.append('{%d,%d,{%s},%d},'%(sum(1<<i for i in t['attached']),sum(1<<i for i in t['ready']),','.join(map(str,t['counters'])),sum(1<<i for i in t['inherited'])))
 return '\n'.join(out+['};','#endif',''])
def normalized(rows):
 from verify_wc3_pathing_work236 import normalize
 scopes=[r for r in rows if r.get('event')=='scope']
 ids={tuple(x['id']):i for i,x in enumerate(scopes[0]['rows'])};groups={}
 out=[]
 for r in scopes:
  members=[]
  for x in r['rows']:
   key=tuple(x['group']);group=None
   if key!=(0xffffffff,0xffffffff):
    if key not in groups:groups[key]=len(groups)
    group=groups[key]
   members.append(dict(unit=ids[tuple(x['id'])],ready=x['ready'],counter=x['counter'],group=group))
  out.append(dict(phase=r['phase'],rows=members))
 return dict(readiness=normalize(rows),scopes=out)
def verify_scopes(events):
 scopes=events['scopes']
 if len(scopes)!=8:raise ValueError('expected pending/success complete scopes')
 for i in(0,4):
  if [r['phase']for r in scopes[i:i+4]]!=['acquire-enter','acquire-leave','release-enter','release-leave']:raise ValueError('unbalanced scopes')
  before,held,release,after=[r['rows']for r in scopes[i:i+4]]
  if any([x['unit']for x in row]!=[0,1]for row in(before,held,release,after)):raise ValueError('lost attachment')
  if [x['counter']+1 for x in before]!=[x['counter']for x in held]or[x['counter']for x in held]!=[x['counter']for x in release]or[x['counter']for x in before]!=[x['counter']for x in after]:raise ValueError('counter imbalance')
  if i==0:
   if any([(x['ready'],x['group'])for x in row]!=[(False,None),(True,0)]for row in(before,held,release,after)):raise ValueError('pending physical owner changed')
  else:
   if any([(x['ready'],x['group'])for x in row]!=[(True,None),(True,0)]for row in(before,held)):raise ValueError('source readiness/old owner differs')
   if any([(x['ready'],x['group'])for x in row]!=[(False,1),(False,1)]for row in(release,after)):raise ValueError('new cohort not bound after readiness')
 pubs=[r for r in events['readiness']if r['event']=='publish']
 if [r['result']for r in pubs]!=[-1,1]or[p['callback']for p in pubs]!=[True,False]:raise ValueError('pending/source publication differs')
def validate_runtime(bundle,spec):
 import verify_wc3_pathing_work236 as prior
 # The unmodified corridor map and public markers remain the earlier contract.
 checked=dict(spec,live_readiness=spec['events']['readiness'])
 saved=prior.SOURCES
 try:
  prior.SOURCES=SOURCES[:2];proof=prior.validate_runtime(bundle,checked)
 finally:prior.SOURCES=saved
 for c in bundle['captures']:
  events=normalized(c['rows']);verify_scopes(events)
  if events!=spec['events']:raise ValueError('frozen queued chronology differs')
 return dict(proof,scope_boundaries=16)
def native_controls(original,controls):
 if [{k:v for k,v in r.items()if k!='trace'}for r in original['rows']]!=[{k:v for k,v in r.items()if k!='trace'}for r in controls['rows']]:raise ValueError('native observer affected results')
 if any(r['old_retained']!=2 or r['old_prepared']!=0 for r in original['rows']):raise ValueError('old owner lifetime differs')
 # Keep the earlier literal physical partition expectations as an independent guard.
 old=json.loads((ROOT/'tools/ghidra/fixtures/retail-work236-1.27.json').read_text())['kernel']
 if any({k:r[k]for k in row}!=row for r,row in zip(original['rows'],old['rows'])):raise ValueError('prior retail partitions changed')
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
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import work249_oracle as native
 fresh=native.original(a.binary);control=native.original(a.binary,False);native_controls(fresh,control)
 if fresh!=bundle['original']or control!=bundle['controls']:raise ValueError('fresh native behavior differs')
 import verify_wc3_pathing_work242 as prior
 before=prior.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:prior.TESTS=TESTS;engine=prior.run_engine(a.test_binary,a.data,a.report)
 finally:prior.TESTS=before
 result=dict(passed=True,task=s['task'],**validate_runtime(bundle,s),original_cases=len(fresh['rows']),original_controls=len(control['rows']),instructions=len(s['instructions']),engine=engine,fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
