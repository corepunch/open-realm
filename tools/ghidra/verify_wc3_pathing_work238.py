#!/usr/bin/env python3
"""Verify independent Alt request readiness and deterministic selected class owners."""
import argparse,hashlib,json,os,re,subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
import verify_wc3_pathing_work237 as prior
from research.work238_oracle import original,header
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work238-1.27.json'
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_alt_request238.h'
SOURCES=['tools/ghidra/research/work238_oracle.py','tools/ghidra/research/Work238Evidence.java',
 'tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py',
 'tools/ghidra/verify_wc3_pathing_work237.py','tools/ghidra/fixtures/retail-work237-1.27.json',
 'tools/ghidra/fixtures/retail-work237-1.27.json.gz','tools/ghidra/verify_wc3_pathing_target_normalize218.py']
TESTS=['wc3_movement.selected238_alt_owns_separate_flight_requests',
 'wc3_movement.selected238_native_class_birth_order',
 'wc3_movement.selected238_alt_callbacks_keep_candidate_order_and_replacement',
 'wc3_movement.selected237_ground_and_flight_share_primary_request',
 'wc3_movement.selected_point_retains_formation_toggle_policy',
 'wc3_movement.partition235_nested_admission_keeps_replacement_owner']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-04.6' or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES) or spec['engine_tests']!=TESTS):raise ValueError('Alt contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('changed source '+p)
 if HEADER.read_text()!=header(spec['kernel']):raise ValueError('native class fixture changed')
 rows=spec['kernel']['rows']
 if len(rows)!=10 or {(r['flight'],r['grounded'])for r in rows}!={(f,g)for f in(0,6,10,9,15)for g in(0,1)}:raise ValueError('missing original domains')
 for row in rows:
  slots=[2 if row['flight']&(1<<i) and not(row['grounded']and i==1)else 0 for i in range(4)]
  if row['slots']!=slots:raise ValueError('attachment classes differ')
  births=row['births'];seen=[];last=-1
  for b in births:
   members=[i for i,s in enumerate(slots)if s==b['request']]
   if not members or b['members']!=members or b['after']!=members[-1] or b['after']<=last or b['flags']!=0x1000e:raise ValueError('native readiness/birth differs')
   seen+=members;last=b['after']
  if sorted(seen)!=list(range(4)):raise ValueError('missing/duplicate physical member')
 return spec

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
   if(len(totals)!=1 or totals[0][0]!=totals[0][1] or int(totals[0][2])!=1 or name not in names or
      any(suite.attrib[k]!='0'for k in('failures','errors','skipped'))):raise ValueError('missing/failed engine test')
   tests+=1;assertions+=int(totals[0][0])
  results[edition]=dict(tests=tests,assertions=assertions)
 return results

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=validate(json.loads(FIXTURE.read_text()));old=prior.validate(json.loads(prior.FIXTURE.read_text()))
 import gzip
 live=prior.validate_runtime(json.loads(gzip.decompress(prior.BUNDLE.read_bytes())),old)
 original_bytes(a.binary,spec['instructions'])
 if original(a.binary)!=spec['kernel']:raise ValueError('fresh original publication differs')
 a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**live,complete_scopes=10,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
