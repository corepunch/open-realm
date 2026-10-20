#!/usr/bin/env python3
"""Verify widget bounds and complete original distance-query exclusion scopes."""
import argparse,gzip,hashlib,json,os,re,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes
from research.work230_oracle import original,header,SHA
from research.work230_scope import original as scopes
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work230-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_region_bounds230.h'
TESTS=['wc3_fine_spatial.widget_region_bounds_match_original_producer_and_cached_restore',
 'pathfinding.captain_distance_excludes_widget_bounds_and_restores_pending_terrain',
 'wc3_save.mixed_sparse_regions_reload_in_owner_order_and_retain_inverse_pixels']
SOURCES=['tools/frida/research/work230_observer.js','tools/frida/research/work228_capture.py',
 'tools/ghidra/research/Work230Evidence.java','tools/ghidra/research/work230_oracle.py',
 'tools/ghidra/research/work230_scope.py','tools/ghidra/verify_wc3_pathing_numeric.py']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(MAP022 [^"\r\n]*)" \)',preload)
def validate(spec):
 if(spec['version']!=1 or spec['task']!='MAP-04.2' or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES) or len(spec['kernels']['cases'])!=96 or
    len(spec['scopes'])!=8 or spec['engine_tests']!=TESTS or len(spec['markers'])!=193):raise ValueError('widget scope contract differs')
 for path,sha in spec['pins'].items():
  if digest(ROOT/path)!=sha:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 prior=ROOT/'tools/ghidra/fixtures/retail-work229-1.27.json'
 if digest(prior)!=spec['prior_fixture_sha256']or json.loads(prior.read_text())['markers']!=spec['markers']:raise ValueError('prior retail expectations changed')
 if HEADER.read_text()!=header(dict(spec['kernels'],scopes=spec['scopes'])):raise ValueError('literal header changed')
 for mode,row in enumerate(spec['scopes']):
  if(row['mode']!=mode or len(row['before'])!=1360 or len(row['after'])!=1360 or
     row['result']!=(0xffffffff if mode&4 else 24) or
     row['events']!=[dict(box=[28,28,37,37],clear=c)for c in(1,0)]):raise ValueError('original scope differs')
  if mode&2 and (row['alias_result']!=0 or row['alias_events']!=[dict(box=[28,28,37,37],clear=c)for c in(1,1,0,0)]):raise ValueError('aliased scope differs')
 return spec

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:raise ValueError('missing repeats/control')
 for c in captures:
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload']
  if(meta['sha256']!=SHA or not meta['owned'] or meta['env']not in('B','C')or
     meta['source_sha256']['map']!=spec['map_sha256']or not footer.get('complete')or footer['markers']!=193 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or markers(preload)!=spec['markers']or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('bad capture')
  if meta['mode']=='observe':
   if(meta['source_sha256']['work230_observer.js']!=spec['pins']['tools/frida/research/work230_observer.js']or
      meta['source_sha256']['work228_capture.py']!=spec['pins']['tools/frida/research/work228_capture.py']or
      [r['value']for r in rows if r.get('event')=='marker']!=spec['markers']):raise ValueError('observer input/markers differ')
   events=[r for r in rows if r.get('event')=='region-bounds'];ends=[r for r in rows if r.get('event')=='trace-end']
   if(events!=[spec['live_bounds']]or len(ends)!=1 or not ends[0]['installed']or ends[0]['count']!=1):raise ValueError('live region bounds differ')
  elif any(r.get('event')in('region-bounds','trace-end','module')for r in rows):raise ValueError('instrumented control')
 return dict(captures=2,controls=1,public_markers=579,live_bounds=2)

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
 if original(a.binary)!=spec['kernels']or scopes(a.binary)!=spec['scopes']:raise ValueError('fresh original execution differs')
 a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**proof,bounds_cases=96,complete_scopes=12,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
