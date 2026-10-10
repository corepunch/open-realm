#!/usr/bin/env python3
"""Check native selected FLOAT request separation and engine queue/save integration."""
import argparse,gzip,hashlib,itertools,json,os,re,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from research.work240_oracle import original,header
from verify_wc3_pathing_work237 import normalize
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work240-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_selected_request240.h'
SOURCES=['tools/frida/research/work240_probe.j','tools/frida/research/work240_make_map.py',
 'tools/frida/research/work237_observer.js','tools/frida/research/work228_capture.py',
 'tools/ghidra/research/work240_oracle.py','tools/ghidra/research/Work240Evidence.java',
 'tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py',
 'tools/ghidra/verify_wc3_pathing_work237.py']
TESTS=['wc3_movement.selected240_all_native_request_classes',
 'wc3_movement.selected240_float_queue_history_and_save',
 'wc3_movement.selected237_ground_and_flight_share_primary_request',
 'wc3_movement.selected238_alt_owns_separate_flight_requests',
 'wc3_movement.selected238_alt_callbacks_keep_candidate_order_and_replacement',
 'wc3_movement.selected239_singleton_clears_player_request_history',
 'wc3_movement.selected_player_input_matches_both_original_journeys',
 'wc3_movement.selected_independent_shift_inputs_match_original_complete_journeys']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-04.6' or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES) or len(spec['instructions'])!=942):
  raise ValueError('selected class contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('changed source '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle differs')
 if HEADER.read_text()!=header(spec['kernel']):raise ValueError('native class header differs')
 rows=spec['kernel']['rows']
 if len(rows)!=48 or {(r['float_mask'],r['flight'],r['grounded'],r['alt'])for r in rows}!=set(itertools.product((0,2,5,15),(0,6,15),(0,1),(0,1))):raise ValueError('missing native domain')
 for r in rows:
  slots=[1 if r['float_mask']&(1<<i)else 2 if r['alt']and r['flight']&(1<<i)and not(r['grounded']and i==1)else 0 for i in range(4)]
  if slots!=r['slots']:raise ValueError('attachment affinity differs')
  seen=[];last=-1
  for b in r['births']:
   members=[i for i,s in enumerate(slots)if s==b['request']]
   if not members or b['members']!=members or b['after']!=members[-1] or b['after']<=last or b['flags']!=0x10000+(14 if r['alt']else 0):raise ValueError('readiness birth differs')
   seen+=members;last=b['after']
  if sorted(seen)!=list(range(4)):raise ValueError('missing physical member')
 return spec

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=4:raise ValueError('missing repeated ordinary/Alt witnesses')
 actual=[]
 for i,c in enumerate(captures):
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload'];markers=re.findall(r'call Preload\( "(P240 [^"\r\n]*)" \)',preload)
  ends=[r for r in rows if r.get('event')=='trace-end']
  if(meta.get('mode')!='observe' or meta.get('sha256')!=SHA or not meta.get('owned') or meta['env']not in('B','C') or
     meta['source_sha256']['map']!=spec['map_sha256'] or len(ends)!=1 or not ends[0]['installed'] or
     any(ends[0][k]!=0 for k in('dispatch','depth')) or ends[0]['count']!=4 or
     footer.get('event')!='preload-file' or not footer['complete'] or footer['markers']!=82 or len(markers)!=82 or
     markers!=[r['value']for r in rows if r.get('event')=='marker'] or hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or
     any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows)):raise ValueError('incomplete capture')
  for source in SOURCES[2:4]:
   if meta['source_sha256'][Path(source).name]!=spec['pins'][source]:raise ValueError('captured source differs')
  n=normalize(rows);actual.append(n)
  slots=[0,1,2 if i>=2 else 0,0]
  if n['flags']!=(24 if i>=2 else 8) or n['order']!=851986 or [a['request']for a in n['attachments']]!=slots:raise ValueError('public request affinity differs')
 if actual!=spec['public'] or actual[0]!=actual[1] or actual[2]!=actual[3]:raise ValueError('frozen public scopes differ')
 return dict(captures=4,public_markers=328,ordinary_cohorts=2,alt_cohorts=3)

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
 spec=validate(json.loads(FIXTURE.read_text()));live=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
 original_bytes(a.binary,spec['instructions'])
 if original(a.binary)!=spec['kernel']:raise ValueError('fresh native request publication differs')
 a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**live,complete_scopes=48,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
