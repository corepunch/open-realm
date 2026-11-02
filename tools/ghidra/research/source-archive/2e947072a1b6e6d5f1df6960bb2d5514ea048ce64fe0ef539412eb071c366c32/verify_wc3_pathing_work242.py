#!/usr/bin/env python3
"""Verify retained queued Move identities through actual Shift/replacement candidate rows."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from verify_wc3_pathing_work241 import verify_live_comparisons
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work242-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/work242_probe.j','tools/frida/research/work242_make_map.py','tools/frida/research/work241_observer.js','tools/frida/research/work228_capture.py','tools/ghidra/research/Work242Evidence.java','tools/ghidra/verify_wc3_pathing_work241.py','tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_movement.selected242_queued_move_identity_survives_save_and_replacement','wc3_movement.selected241_original_comparator_words','wc3_movement.selected241_public_candidate_order_matches_retail_rows','wc3_movement.selected240_float_queue_history_and_save','wc3_order_lifecycle.*','wc3_interrupt.*']
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def normalize(rows):
 starts=[i for i,r in enumerate(rows)if r.get('event')=='packet'];out=[]
 for i,start in enumerate(starts):
  r=rows[start:starts[i+1]if i+1<len(starts)else len(rows)];attachments=[x for x in r if x.get('event')=='attach'];units={x['unit']:n for n,x in enumerate(attachments)}
  candidates=[x for x in r if x.get('event')=='candidate'];scores=[x for x in r if x.get('event')=='score'];poses=[x for x in r if x.get('event')=='score-pose'];publications=[x for x in r if x.get('event')=='publish']
  if len(units)!=4 or any(len(v)!=4 for v in(candidates,scores,poses,publications)):raise ValueError('missing queued candidate scope')
  if any(x['unit']not in units for x in publications):raise ValueError('unknown published candidate')
  keys=[]
  for n,(c,s,p)in enumerate(zip(candidates,scores,poses)):
   if units[c['unit']]!=n or c['unit']!=s['unit']or c['row'][3]!=s['value']or c['row'][2]!=c['unit1b4']or p['words'][2]!=0:raise ValueError('queued count/score provenance differs')
   keys.append([c['unit198'],*c['row'][1:6],c['canonical']])
  out.append(dict(flags=rows[start]['words'][6]&0xffff,order=rows[start]['words'][7],keys=keys,point=[s['point']for s in scores],poses=[p['words']for p in poses],published=[units[x['unit']]for x in publications]))
 return out

def validate(spec):
 if(spec['version']!=1 or spec['task']!='ORDER-02.2' or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS or len(spec['instructions'])<500):raise ValueError('queued identity contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('changed source '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle differs')
 return spec

def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=2:raise ValueError('missing queued repeats')
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload'];markers=re.findall(r'call Preload\( "(P242 [^"\r\n]*)" \)',preload);ends=[r for r in rows if r.get('event')=='trace-end']
  if(meta.get('mode')!='observe'or meta.get('sha256')!=SHA or not meta.get('owned')or meta['env']not in('B','C')or meta['source_sha256']['map']!=spec['map_sha256']or
   footer.get('event')!='preload-file'or not footer.get('complete')or footer['markers']!=82 or len(markers)!=82 or markers!=[r['value']for r in rows if r.get('event')=='marker']or hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or
   len(ends)!=1 or not ends[0]['installed']or ends[0]['dispatch']or ends[0]['depth']or ends[0]['count']!=8 or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('incomplete queued capture')
  for p in SOURCES[2:4]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured runner/observer differs')
  inputs=[r for r in rows if r.get('event')=='player-input']
  if len(inputs)!=2 or any(r['rc']for r in inputs)or [r['plan']for r in inputs]!=meta['input']or [r['plan']['shift']for r in inputs]!=[True,False]:raise ValueError('missing accepted Shift/replacement')
  actual=normalize(rows)
  if actual!=spec['public'][i]or len(actual)!=2:raise ValueError('frozen queued rows differ')
  for n,r in enumerate(actual):
   if r['order']!=851986 or r['flags']!=(9 if n==0 else 8)or [k[1:3]for k in r['keys']]!=[[n+1,n+1]]*4 or r['published']!=[0,3,1,2]:raise ValueError('pending user head omitted')
 return dict(captures=2,public_markers=164,public_packets=4)

def run_engine(binary,data,report):
 import os,subprocess,xml.etree.ElementTree as ET
 results={}
 for edition in('classic','tft'):
  tests=assertions=0
  for i,name in enumerate(TESTS):
   log=report.with_name(report.stem+'-'+edition+'-'+str(i)+'.log');junit=log.with_suffix('.xml')
   env=dict(os.environ,TEST_JUNIT=str(junit),LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
   args=[str(binary.resolve()),'-data',str(data.resolve())]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test',name]
   with log.open('w')as f:subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180,check=True)
   suite=ET.parse(junit).getroot();totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
   if len(totals)!=1 or totals[0][0]!=totals[0][1]or int(totals[0][2])<1 or any(suite.attrib[k]!='0'for k in('failures','errors','skipped')):raise ValueError('missing/failed queue engine test')
   tests+=int(totals[0][2]);assertions+=int(totals[0][0])
  results[edition]=dict(tests=tests,assertions=assertions)
 return results

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=validate(json.loads(FIXTURE.read_text()));live=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec);original_bytes(a.binary,spec['instructions']);comparisons=verify_live_comparisons(a.binary,spec['public']);a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**live,public_comparisons=comparisons,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
