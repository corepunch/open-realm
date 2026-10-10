#!/usr/bin/env python3
"""Verify queued cohort selectors, complete circle query and engine consumers."""
import argparse,gzip,hashlib,json,os,re,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from research.work232_oracle import original
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work232-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/work232_observer.js','tools/frida/research/work228_capture.py',
 'tools/ghidra/research/work232_oracle.py','tools/ghidra/research/Work232Evidence.java',
 'tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py',
 'tools/ghidra/verify_wc3_pathing_target_normalize218.py']
TESTS=['wc3_movement.cohort232_joins_first_spatial_peer_without_scanning_other_groups',
 'wc3_movement.cohort232_circle_is_exact_and_does_not_include_candidate_radius',
 'wc3_movement.cohort232_requires_authored_type_not_equal_pathing_masks',
 'wc3_movement.cohort232_materializes_before_mutating_or_nested_callbacks',
 'wc3_movement.selected_shift_input_matches_original_staggered_activation',
 'wc3_movement.selected_two_shift_inputs_match_original_three_leg_journey']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-04.6' or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES) or len(spec['kernel']['rows'])!=8 or spec['engine_tests']!=TESTS):raise ValueError('cohort contract differs')
 for path,sha in spec['pins'].items():
  if digest(ROOT/path)!=sha:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 return spec

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=2:raise ValueError('missing repeat')
 for c in captures:
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload']
  markers=re.findall(r'call Preload\( "(PATHTRACE [^"\r\n]*)" \)',preload)
  if(meta['mode']!='observe' or meta['sha256']!=SHA or not meta['owned'] or meta['env']not in('B','C') or
     meta['source_sha256']['map']!=spec['map_sha256'] or not footer.get('complete') or footer['markers']!=305 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or len(markers)!=305 or
     markers!=[r['value']for r in rows if r.get('event')=='marker'] or ' label=complete'not in markers[-1] or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('bad capture')
  for name in SOURCES[:2]:
   if meta['source_sha256'][Path(name).name]!=spec['pins'][name]:raise ValueError('captured source differs')
  modules=[r for r in rows if r.get('event')=='module'];ends=[r for r in rows if r.get('event')=='trace-end']
  if len(modules)!=1 or len(ends)!=1 or not ends[0]['installed'] or ends[0]['searches']!=2:raise ValueError('missing scope')
  base=int(modules[0]['base'],16);active=[];queries=[];searches=[]
  for r in rows:
   event=r.get('event')
   if event=='query':
    if active or queries:raise ValueError('overlapping search')
    queries.append(r)
   elif event=='candidate':
    if not queries:raise ValueError('candidate outside query')
    active.append(r)
   elif event=='cohort':
    if len(queries)!=1 or len(active)!=2 or r['radius']!=spec['kernel']['radius_word']:raise ValueError('incomplete search')
    w=queries[0]['words']
    if(w[0]-base!=0x5faaf0 or w[2:4]!=[8,1148846080] or w[4]!=int(r['source'],16)+0x164 or w[5:]!=[24,3,31]):raise ValueError('selector meaning differs')
    if [x['result']for x in active]!=([1,1]if not searches else[1,0]) or r['result']!=len(searches):raise ValueError('first success differs')
    for x in active:
     ctx=x['context']
     if(ctx[0]!=int(r['source'],16) or ctx[1:3]!=r['point'] or ctx[5:7]!=r['previous'] or
        x['previous']!=r['previous'] or ctx[9]!=r['category'] or x['category']!=r['category'] or
        x['accepted']!=int(x['result']==0) or (x['result']==0 and x['unit']==r['source'])):raise ValueError('cohort history/type differs')
    searches.append(r);active.clear();queries.clear()
  if len(searches)!=2 or active or queries:raise ValueError('missing search completion')
  inputs=[r for r in rows if r.get('event')=='player-input']
  if len(inputs)!=1 or inputs[0]['rc']!=0 or not inputs[0]['plan']['shift']:raise ValueError('missing public Shift')
 return dict(captures=2,live_queries=4,live_candidates=8)

def run_engine(binary,data,report):
 results={}
 for edition in('classic','tft'):
  tests=assertions=0
  for i,name in enumerate(TESTS):
   log=report.with_name(report.stem+'-'+edition+'-'+str(i)+'.log');junit=log.with_suffix('.xml')
   env=dict(os.environ,TEST_JUNIT=str(junit),LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
   args=[str(binary.resolve()),'-data',str(data.resolve())]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test',name]
   with log.open('w')as f:subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180,check=True)
   suite=ET.parse(junit).getroot();names={t.attrib['name']for t in suite.findall('.//testcase')}
   totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
   if(len(totals)!=1 or totals[0][0]!=totals[0][1]or int(totals[0][2])!=1 or name not in names or
      any(suite.attrib[k]!='0'for k in('failures','errors','skipped'))):raise ValueError('missing/failed engine test')
   tests+=1;assertions+=int(totals[0][0])
  results[edition]=dict(tests=tests,assertions=assertions)
 return results

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=validate(json.loads(FIXTURE.read_text()));proof=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
 original_bytes(a.binary,spec['instructions'])
 if original(a.binary)!=spec['kernel']:raise ValueError('fresh original execution differs')
 a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**proof,complete_scopes=8,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
