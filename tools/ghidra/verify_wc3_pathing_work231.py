#!/usr/bin/env python3
"""Verify original point-query scopes and read-only native repeats/control."""
import argparse,gzip,hashlib,json,os,re,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from research.work231_oracle import original,header
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work231-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_point_query231.h'
TESTS=['wc3_api.terrain_point_query231_preserves_live_spatial_observations',
 'wc3_fine_spatial.point_query231_matches_original_counted_scope_and_stamps',
 'wc3_api.terrain_pathing_query_ignores_objects_and_preserves_native_amphibious_bit',
 'wc3_api.terrain_pathing_natives_survive_save_and_restore_blight']
SOURCES=['tools/frida/research/work231_observer.js','tools/frida/research/work231_probe.j',
 'tools/frida/research/work231_make_map.py','tools/frida/research/work228_capture.py',
 'tools/ghidra/research/Work231Evidence.java','tools/ghidra/research/work231_oracle.py',
 'tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py',
 'tools/ghidra/verify_wc3_pathing_numeric.py','tools/ghidra/verify_wc3_pathing_target_normalize218.py']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(P231 [^"\r\n]*)" \)',preload)
def normalized(row):
 return {k:v for k,v in row.items()if k not in('stamp','stamp_after')}|{'stamp_delta':row['stamp_after']-row['stamp']}
def validate(spec):
 if(spec['version']!=1 or spec['task']!='MAP-04.2' or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES) or len(spec['kernels'])!=600 or len(spec['sequence'])!=48 or
    spec['engine_tests']!=TESTS or len(spec['markers'])!=14):raise ValueError('point scope contract differs')
 for path,sha in spec['pins'].items():
  if digest(ROOT/path)!=sha:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 if HEADER.read_text()!=header(spec['kernels']):raise ValueError('literal header changed')
 return spec

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:raise ValueError('missing repeats/control')
 for c in captures:
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload']
  if(meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or
     meta['source_sha256']['map']!=spec['map_sha256']or not footer.get('complete')or footer['markers']!=14 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or markers(preload)!=spec['markers']or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('bad capture')
  if meta['mode']=='observe':
   if(meta['source_sha256']['work231_observer.js']!=spec['pins'][SOURCES[0]]or
      meta['source_sha256']['work228_capture.py']!=spec['pins'][SOURCES[3]]or
      [r['value']for r in rows if r.get('event')=='marker']!=spec['markers']):raise ValueError('observer input/markers differ')
   queries=[r for r in rows if r.get('event')=='point'];ends=[r for r in rows if r.get('event')=='trace-end']
   if(len(ends)!=1 or not ends[0]['installed']or ends[0]['count']!=48 or len(queries)!=48 or
      [normalized(r)for r in queries]!=spec['sequence']):raise ValueError('live point traversal differs')
   # Every query belongs to one explicit synchronous public-native window.
   active=False;n=0
   for r in rows:
    if r.get('event')=='marker':
     if active and n!=8:raise ValueError('incomplete native window')
     active=' begin='in r['value'];n=0
    elif r.get('event')=='point':
     if not active or r['mode_after']!=r['mode']:raise ValueError('query outside window/mode leak')
     n+=1
  elif any(r.get('event')in('point','trace-end','module','marker')for r in rows):raise ValueError('instrumented control')
 return dict(captures=2,controls=1,public_markers=42,live_queries=96)

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
 if original(a.binary)!=spec['kernels']:raise ValueError('fresh original execution differs')
 a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**proof,complete_scopes=600,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
