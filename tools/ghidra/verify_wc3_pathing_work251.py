#!/usr/bin/env python3
"""Verify visibility policy branches and native optional-target normalization."""
import argparse,gzip,hashlib,json,re,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
sys.path.insert(0,str(ROOT/'tools/frida/research'))
from verify_work251_visibility import verify as original_visibility
from verify_wc3_pathing_blink import verify as original_validation
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work251-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER='games/warcraft-3/game/tests/fixtures/retail_visibility251.h'
SOURCES=['tools/frida/research/work251_'+s for s in('observer.js','probe.j','make_map.py')]+[
 'tools/frida/research/target218_capture.py','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/ghidra/research/Work251Evidence.java','tools/ghidra/research/verify_work251_visibility.py',
 'tools/ghidra/verify_wc3_pathing_blink.py','tools/ghidra/fixtures/retail-blink-validation182-1.27.json',
 'tools/ghidra/fixtures/retail-visibility251-types-1.27.json','tools/frida/research/target031_branch_table.py',
 'tools/ghidra/fixtures/research/TARGET-03.1-expected.json','tools/ghidra/fixtures/research/TARGET-03.2-expected.json',
 'tools/ghidra/fixtures/research/TARGET-03.2-expected-reacquire.json',HEADER]
TESTS=['wc3_movement.target*','wc3_movement.queued250*','wc3_combat.*','wc3_api.fog*',
 'wc3_fow.*','wc3_spell.permanent_invisibility*','wc3_spell.true_sight*','wc3_spell.ghost*','wc3_spell.invisibility*','wc3_spell.wind_walk*','wc3_spell.far_sight*','wc3_shadowmeld.*','wc3_order_lifecycle.*','wc3_interrupt.*','wc3_save.rejects_prior_save_versions']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(s):return re.findall(r'call Preload\( "(P251 [^"\r\n]*)" \)',s)
def normalize(rows):return [r for r in rows if r['event']in('marker','factory','query','admission','target-order')]
def verify_runtime(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('two observations and control required')
 sequences=[]
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta=rows[0];footer=rows[-1];public=markers(c['preload'])
  if(meta.get('mode')!=('control'if i==2 else'observe')or meta.get('task')!='TARGET-03.1'or meta.get('sha256')!=SHA or
     not meta.get('owned')or meta.get('env')not in('B','C')or meta['source_sha256']['map']!=spec['map_sha256']or
     not footer.get('complete')or footer.get('event')!='preload-file'or footer.get('markers')!=39 or len(public)!=39 or public!=spec['markers']or
     hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']or any(r.get('event')=='trace-failed'or r.get('type')=='error'for r in rows)):
   raise ValueError('unowned, changed or incomplete original capture')
  for p in SOURCES[:1]+SOURCES[3:6]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs')
  for k,h in spec['helper_sha256'].items():
   if meta['source_sha256'][k]!=h:raise ValueError('owned helper differs')
  if i==2:
   if normalize(rows)or any(r['event']=='trace-end'for r in rows):raise ValueError('control instrumented')
   continue
  ends=[r for r in rows if r['event']=='trace-end']
  if len(ends)!=1 or ends[0]!={'event':'trace-end','installed':True,'readOnly':True,'counts':{'marker':39,'factory':13,'admission':4,'target-order':5,'query':63}}:raise ValueError('incomplete observer')
  if [r['value']for r in rows if r['event']=='marker']!=public:raise ValueError('public observer differs')
  seq=normalize(rows)
  if seq!=spec['sequence']:raise ValueError('native admission chronology differs')
  admissions=[r for r in seq if r['event']=='admission'];orders=[r for r in seq if r['event']=='target-order']
  if [r['result']for r in admissions]!=[0xdd,0xaa,0xdd,0]or any(r['flags']!=6 for r in admissions)or [r['scene']for r in orders]!=[0,2,4,6,7]or any(r['target']is not None for r in orders[:3])or any(r['target']is None for r in orders[3:]):raise ValueError('invalid optional target was retained')
  sequences.append(seq)
 if sequences[0]!=sequences[1]:raise ValueError('repeat differs')
 return dict(captures=2,controls=1,public_markers=117)
def verify_matrix(matrix,header):
 if not matrix['passed']or matrix['binary_sha256']!=SHA or len(matrix['cases'])!=6912 or len({tuple(r[:9])for r in matrix['cases']})!=6912:raise ValueError('matrix incomplete')
 expected=[[r[i]for i in(0,1,3,4,5,6,7,9)]for r in matrix['cases']if r[2]==2 and r[8]==4 and r[0]<4]
 actual=[[int(v)for v in re.findall(r'\d+',line)]for line in header.splitlines()if line.startswith('    {')]
 if len(actual)!=576 or actual!=expected:raise ValueError('engine visibility fixture differs from original')
 return len(actual)
def verify_branches():
 import target031_branch_table as t
 table=json.loads((ROOT/SOURCES[12]).read_text());exps=[]
 for i,p in enumerate(SOURCES[13:15]):
  e=json.loads((ROOT/p).read_text());e['_name']='expected-TARGET-03.2'+('-reacquire'if i else'')+'.json';exps.append(e)
 rebuilt=[]
 for rid,fn,condition,result,asm,pred in t.ROWS:
  witnesses=[]
  if pred:
   for e in exps:witnesses+=t.find(e,pred)
  rebuilt.append(dict(id=rid,function='6f'+fn,condition=condition,result=result,asm=asm,evidence='A+L'if witnesses else'A',witnesses=witnesses[:3],witness_scenes=len(witnesses)))
 if rebuilt!=table['rows']or len(rebuilt)!=36:raise ValueError('frozen handoff branch table differs')
 return len(rebuilt)
def validate(spec):
 if spec['version']!=1 or spec['task']!='TARGET-03.1'or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS:raise ValueError('contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source changed '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle changed')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));verify_runtime(bundle,spec);verify_matrix(bundle['visibility'],(ROOT/HEADER).read_text());verify_branches();return bundle

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(FIXTURE.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
 if original_visibility(a.binary)!=bundle['visibility']:raise ValueError('original visibility matrix changed')
 validation=original_validation(a.binary)
 if validation!=json.loads((ROOT/SOURCES[9]).read_text()):raise ValueError('frozen native validation expectation changed')
 import verify_wc3_pathing_work242 as runner
 saved=runner.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=saved
 result=dict(passed=True,task=spec['task'],**verify_runtime(bundle,spec),matrix_cases=6912,engine_matrix_cases=576,validation_cases=len(validation['validation_cases'])+1,branch_rows=verify_branches(),instructions=len(spec['instructions']),engine=engine,binary_sha256=SHA,fixture_sha256=digest(FIXTURE),test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
