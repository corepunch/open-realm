#!/usr/bin/env python3
"""Verify queued source readiness and repartition through original callback code."""
import argparse,gzip,hashlib,json,os,re,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from research.work236_oracle import original,header
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work236-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
HEADER=ROOT/'games/warcraft-3/game/tests/fixtures/retail_queued_partition236.h'
SOURCES=['tools/frida/research/work236_observer.js','tools/frida/research/work228_capture.py',
 'tools/ghidra/research/work236_oracle.py','tools/ghidra/research/Work236Evidence.java',
 'tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py',
 'tools/ghidra/verify_wc3_pathing_target_normalize218.py']
TESTS=['wc3_movement.queued236_activation_repartitions_inherited_rows',
 'wc3_movement.queued236_relocated_peer_split_saves_and_cancels_independently',
 'wc3_movement.partition235_matches_complete_native_cohort_recursion',
 'wc3_movement.partition235_public_order_partitions_and_saves_physical_owners',
 'wc3_movement.cohort232_joins_first_spatial_peer_without_scanning_other_groups',
 'wc3_movement.cohort232_circle_is_exact_and_does_not_include_candidate_radius']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-04.6' or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES) or len(spec['kernel']['rows'])!=48 or spec['engine_tests']!=TESTS):raise ValueError('queued contract differs')
 for path,sha in spec['pins'].items():
  if digest(ROOT/path)!=sha:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 if HEADER.read_text()!=header(spec['kernel']):raise ValueError('original expected header changed')
 return spec

def normalize(rows):
 pubs=[r for r in rows if r.get('event')=='publish'];ready=[r for r in rows if r.get('event')=='ready'];sets=[r for r in rows if r.get('event')=='set-ready']
 if len(pubs)!=2 or len(ready)!=1 or len(sets)!=1:raise ValueError('missing readiness/publication boundary')
 final=pubs[1]['before'];ids={tuple(key):i for i,key in enumerate(final)if key is not None}
 if len(final)!=12 or len(ids)!=2 or any(final[2:]):raise ValueError('canonical ready candidates differ')
 if any(r['request']!=pubs[0]['request']for r in [*pubs,*sets]):raise ValueError('request identity changed')
 out=[]
 for r in rows:
  if r.get('event')=='ready':out.append(dict(event='ready',callback=r['callback'],mover=ids[tuple(r['mover'])]))
  elif r.get('event')=='set-ready':out.append(dict(event='set-ready',callback=r['callback'],mover=ids[tuple(r['mover'])],value=r['value']))
  elif r.get('event')=='publish':out.append(dict(event='publish',callback=r['callback'],result=r['result'],before=[ids[tuple(k)]if k else None for k in r['before']],after=[ids[tuple(k)]if k else None for k in r['after']]))
  elif r.get('event')=='candidate':out.append(dict(event='candidate',result=r['result'],accepted=r['accepted']))
 return out

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=2:raise ValueError('missing repeat')
 for i,c in enumerate(captures):
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload']
  markers=re.findall(r'call Preload\( "(PATHTRACE [^"\r\n]*)" \)',preload)
  if(meta['mode']!='observe' or meta['sha256']!=SHA or not meta['owned'] or meta['env']not in('B','C') or
     meta['source_sha256']['map']!=spec['map_sha256'] or footer.get('event')!='preload-file' or
     not footer.get('complete') or footer['markers']!=305 or len(markers)!=305 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or
     markers!=[r['value']for r in rows if r.get('event')=='marker'] or
     hashlib.sha256(json.dumps(markers).encode()).hexdigest()!=spec['marker_sha256'][i] or
     any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows)):raise ValueError('bad capture')
  for name in SOURCES[:2]:
   if meta['source_sha256'][Path(name).name]!=spec['pins'][name]:raise ValueError('captured source differs')
  ends=[r for r in rows if r.get('event')=='trace-end'];inputs=[r for r in rows if r.get('event')=='player-input']
  if(len(ends)!=1 or not ends[0]['installed'] or ends[0]['requests']!=2 or ends[0]['searchDepth'] or ends[0]['callbackDepth'] or
     len(inputs)!=1 or inputs[0]['rc']!=0 or inputs[0]['plan']!=spec['input'] or
     normalize(rows)!=spec['live_readiness']):raise ValueError('queued readiness differs')
 return dict(captures=2,pending_publications=2,source_publications=2,public_markers=610)

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
 spec=validate(json.loads(FIXTURE.read_text()));proof=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
 original_bytes(a.binary,spec['instructions'])
 if original(a.binary)!=spec['kernel']:raise ValueError('fresh original execution differs')
 a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**proof,complete_scopes=48,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
