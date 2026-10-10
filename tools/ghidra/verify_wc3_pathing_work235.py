#!/usr/bin/env python3
"""Verify recursive ready-member cohorts and saved physical owners."""
import argparse,gzip,hashlib,json,os,re,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from research.work235_oracle import original,header
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work235-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/work235_observer.js','tools/frida/research/work228_capture.py',
 'tools/frida/research/work235_make_map.py','tools/frida/research/work235_probe.j',
 'tools/ghidra/research/work235_oracle.py','tools/ghidra/research/Work235Evidence.java',
 'tools/ghidra/research/route01_1_2_harness.py',
 'tools/ghidra/verify_wc3_pathing_target_normalize218.py']
TESTS=['wc3_movement.partition235_matches_complete_native_cohort_recursion',
 'wc3_movement.partition235_public_order_partitions_and_saves_physical_owners',
 'wc3_movement.partition235_captain_split_preserves_shared_owner_and_replacement',
 'wc3_movement.partition235_nested_admission_keeps_replacement_owner',
 'wc3_movement.source233_matches_original_preference_prediction_and_bypass',
 'wc3_movement.source233_birth_and_flight_rebind_choose_enabled_member',
 'wc3_movement.disabled234_native_group_route_retains_destination_without_admission',
 'wc3_movement.disabled234_flight_rebind_group_caches_and_saves_route']
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_group_partition235.h'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-04.6' or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES) or len(spec['kernel']['rows'])!=192 or spec['engine_tests']!=TESTS):raise ValueError('partition contract differs')
 for path,sha in spec['pins'].items():
  if digest(ROOT/path)!=sha:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 if HEADER.read_text()!=header(spec['kernel']):raise ValueError('original expected header changed')
 return spec

def normalize(rows):
 binds=[r for r in rows if r.get('event')=='bind'];maps={}
 for phase in range(3):
  roots=[r for r in binds if r['phase']==phase and r['depth']==0 and r['index']==0]
  if len(roots)!=1 or len(roots[0]['candidates'])!=12 or any(roots[0]['candidates'][3:]):raise ValueError('missing canonical candidates')
  ids=[tuple(c['identity'])for c in roots[0]['candidates'][:3]]
  if len(set(ids))!=3:raise ValueError('aliased candidates')
  maps[phase]={key:i for i,key in enumerate(ids)}
 out=[]
 for r in binds:
  ids=maps[r['phase']]
  out.append(dict(phase=r['phase'],depth=r['depth'],index=r['index'],flags=r['flags'],
   candidates=[dict(index=ids[tuple(c['identity'])],pose=c['pose'],path=c['path'])if c else None for c in r['candidates']],
   members=[ids[tuple(m)]for m in r['members']]))
 return out

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=3:raise ValueError('missing repeat/control')
 for i,c in enumerate(captures):
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload']
  markers=re.findall(r'call Preload\( "(P235 [^"\r\n]*)" \)',preload)
  if(meta['mode']!=('control'if i==2 else'observe') or meta['sha256']!=SHA or not meta['owned'] or
     meta['env']not in('B','C') or meta['source_sha256']['map']!=spec['map_sha256'] or
     footer.get('event')!='preload-file' or not footer.get('complete') or footer['markers']!=8 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or markers!=spec['markers'] or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('bad capture')
  for name in SOURCES[:2]:
   if meta['source_sha256'][Path(name).name]!=spec['pins'][name]:raise ValueError('captured source differs')
  binds=[r for r in rows if r.get('event')=='bind'];ends=[r for r in rows if r.get('event')=='trace-end'];modules=[r for r in rows if r.get('event')=='module']
  if i==2:
   if binds or ends or modules:raise ValueError('control was hooked')
  elif(len(modules)!=1 or len(ends)!=1 or not ends[0]['installed'] or ends[0]['count']!=9 or ends[0]['depth'] or
       normalize(rows)!=spec['live_binds'] or markers!=[r['value']for r in rows if r.get('event')=='marker']):raise ValueError('cohort construction differs')
 return dict(captures=3,live_binds=18,physical_cohorts=10,public_markers=8)

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
 result=dict(passed=True,task=spec['task'],**proof,complete_scopes=192,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
