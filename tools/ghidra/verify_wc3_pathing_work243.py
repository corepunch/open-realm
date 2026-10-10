#!/usr/bin/env python3
"""Certify prepared idle Shift admission separately from delayed reconstruction."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from verify_wc3_pathing_work241 import verify_live_comparisons
from verify_wc3_pathing_work242 import normalize as normalize_candidates
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work243-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/work243_probe.j','tools/frida/research/work243_make_map.py','tools/frida/research/work243_observer.js','tools/frida/research/work228_capture.py','tools/ghidra/research/Work243Evidence.java','tools/ghidra/verify_wc3_pathing_work241.py','tools/ghidra/verify_wc3_pathing_work242.py','tools/ghidra/research/foot03_spatial_harness_copy.py']
TESTS=['wc3_movement.selected243*','wc3_movement.selected240_float_queue_history_and_save','wc3_movement.selected238_alt_callbacks_keep_candidate_order_and_replacement','wc3_movement.queued*','wc3_order_lifecycle.*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def normalize(rows):
 attached=[r for r in rows if r.get('event')=='attach'];units={r['unit']:i for i,r in enumerate(attached)};movers={tuple(r['mover']):i for i,r in enumerate(attached)}
 retained=[r for r in rows if r.get('event')=='retain'];requests={tuple(r['request']):units[r['unit']]for r in retained}
 # Class label is its first attached candidate, independent of allocator handles.
 for r in retained:requests[tuple(r['request'])]=min(units[x['unit']]for x in retained if x['request']==r['request'])
 result=[]
 for r in rows:
  kind=r.get('event')
  if kind=='ready':result.append(dict(event=kind,tick=r['tick'],request=requests.get(tuple(r['request']),-1),unit=movers[tuple(r['mover'])]))
  elif kind=='publish-ready':result.append(dict(event=kind,tick=r['tick'],request=requests.get(tuple(r['request']),-1),flags=r['flags'],ready=[i for i,v in enumerate(r['slots'])if v!=0xffffffff]))
  elif kind=='publish-ready-end':result.append(dict(event=kind,tick=r['tick'],result=r['result']))
  elif kind=='bind':result.append(dict(event=kind,tick=r['tick'],request=requests.get(tuple(r['request']),-1),depth=r['depth'],index=r['index'],flags=r['flags'],members=[movers[tuple(v)]for v in r['members']]))
 return dict(public=normalize_candidates(rows),timeline=result)
def validate(s):
 if s['version']!=1 or s['task']!='GROUP-04.6' or s['game_sha256']!=SHA or set(s['pins'])!=set(SOURCES)or s['engine_tests']!=TESTS or len(s['instructions'])!=391:raise ValueError('prepared admission contract differs')
 for p,h in s['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('changed source '+p)
 if digest(BUNDLE)!=s['bundle_sha256']:raise ValueError('capture bundle differs')
 return s
def validate_runtime(b,s):
 if len(b['captures'])!=4:raise ValueError('missing ordinary/Alt repeats')
 for i,c in enumerate(b['captures']):
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload'];markers=re.findall(r'call Preload\( "(P243 [^"\r\n]*)" \)',preload);ends=[r for r in rows if r.get('event')=='trace-end']
  if(meta.get('mode')!='observe'or meta.get('sha256')!=SHA or not meta.get('owned')or meta['env']not in('B','C')or meta['source_sha256']['map']!=s['map_sha256']or
   footer.get('event')!='preload-file'or not footer.get('complete')or footer['markers']!=242 or len(markers)!=242 or markers!=[r['value']for r in rows if r.get('event')=='marker']or hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or
   len(ends)!=1 or not ends[0]['installed']or ends[0]['dispatch']or ends[0]['depth']or ends[0]['count']!=5 or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('incomplete prepared capture')
  for p in SOURCES[2:4]:
   if meta['source_sha256'][Path(p).name]!=s['pins'][p]:raise ValueError('observer/runner differs')
  inputs=[r for r in rows if r.get('event')=='player-input']
  if len(inputs)!=1 or inputs[0]['rc']or [inputs[0]['plan']]!=meta['input']or not inputs[0]['plan']['shift']or inputs[0]['plan']['alt']!=(i<2):raise ValueError('missing genuine Shift input')
  actual=normalize(rows)
  if actual!=s['public'][i]:raise ValueError('frozen prepared chronology differs')
  packet=actual['public'][0];timeline=actual['timeline'];flags=14 if i<2 else 0
  if len(actual['public'])!=1 or packet['flags']!=(25 if i<2 else 9)or packet['published']!=[3,1,2,0]or [k[1:3]for k in packet['keys']]!=[[1,1],[0,0],[0,0],[0,0]]:raise ValueError('mixed busy/idle admission differs')
  ready=[r for r in timeline if r['event']=='ready']
  if [r['unit']for r in ready]!=[3,1,2]:raise ValueError('busy member incorrectly ready')
  binds=[r for r in timeline if r['event']=='bind'];roots=[r for r in binds if r['depth']==0]
  expected=[[0],[1],[2],[3],[0]]if i<2 else [[0],[1],[2,3],[0]]
  if [r['members']for r in roots]!=expected or any(r['flags']!=flags for r in roots[1:-1])or roots[-1]['flags']!=0 or roots[-1]['request']!=-1 or roots[-1]['tick']<=ready[-1]['tick']:raise ValueError('prepared policy leaks across delayed activation')
 return dict(captures=4,public_markers=968,public_packets=4)
def run_engine(binary,data,report):
 import verify_wc3_pathing_work242 as prior
 before=prior.TESTS
 try:prior.TESTS=TESTS;return prior.run_engine(binary,data,report)
 finally:prior.TESTS=before
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 s=validate(json.loads(FIXTURE.read_text()));live=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),s);original_bytes(a.binary,s['instructions']);comparisons=verify_live_comparisons(a.binary,[r['public']for r in s['public']]);a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=s['task'],**live,public_comparisons=comparisons,instructions=len(s['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
