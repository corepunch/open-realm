#!/usr/bin/env python3
"""Verify retail target-availability delivery and actual engine owner acquisition."""
from retail_capture_source import matches_capture_source
import argparse,copy,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
import verify_wc3_pathing_work242 as runner
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-availability260-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/cap260_capture.py','tools/frida/research/cap260_observer.js',
 'tools/frida/research/cap260_probe.j','tools/frida/research/cap260_make_map.py',
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/ghidra/research/Availability260Evidence.java']
TESTS=['wc3_order_lifecycle.*','wc3_food.*','wc3_proximity.*',
 'wc3_movement.group_move_attack*','wc3_movement.group_commit_speed_matches_original_policy_boundaries','wc3_movement.persistent_group_commit_matches_moving_target_speed']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(C260 [^"\r\n]*)" \)',text)
def projection(rows):
 result=[]
 for row in rows:
  if row.get('event')not in ('marker','available-enter','available-exit','notify-enter','notify-exit','range','admission','begin','trace-end'):continue
  row=copy.deepcopy(row)
  # Unit284/288 are unsynchronized cached world samples. Preserve raw words in
  # the bundle; compare committed public positions and actual range decisions.
  for key in ('receiver','source','target'):
   if isinstance(row.get(key),dict):
    row[key].pop('x',None);row[key].pop('y',None)
  result.append(row)
 return result

def validate(spec):
 if spec['version']!=1 or spec['task']!='GROUP-03.2' or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES):raise ValueError('availability contract differs')
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('original bundle differs')
 for path,sha in spec['pins'].items():
  if not matches_capture_source(ROOT,path,sha):raise ValueError('source differs: '+path)
 if len(spec['instructions'])!=1355 or len(spec['markers'])!=10:raise ValueError('instruction/public coverage differs')
 return spec

def runtime(bundle,spec):
 cases=bundle['captures']
 if len(cases)!=3 or [c['rows'][0]['mode']for c in cases]!=['observe','observe','control']:raise ValueError('repeat/control absent')
 for c in cases:
  rows=c['rows'];meta,end=rows[0],rows[-1]
  if (meta['task']!=spec['task'] or meta['sha256']!=SHA or not meta['owned'] or meta['env']not in ('B','C') or
      meta['source_sha256']!={**spec['capture_sources'],'map':spec['map_sha256']} or
      any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows)):raise ValueError('capture provenance/failure')
  if end!={'event':'preload-file','sha256':hashlib.sha256(c['preload'].encode()).hexdigest(),'markers':10,'complete':True} or markers(c['preload'])!=spec['markers']:raise ValueError('public control differs')
  if meta['mode']=='control':
   if projection(rows) or any(r.get('event')=='module'for r in rows):raise ValueError('control instrumented')
   continue
  if projection(rows)!=spec['events']:raise ValueError('notification stream differs')
  begins=[(r['scene'],r['receiver'],r['caller'])for r in rows if r.get('event')=='begin']
  if begins!=[(0,1,'49e351'),(0,3,'49e351'),(1,2,'49e351'),(1,5,'49e351'),(2,6,'49e351')]:raise ValueError('acquisition/exemption producer differs')
  if [(r['radius'],r['caller'])for r in rows if r.get('event')=='available-enter']!=[(0x44898000,'6991a7')]*4:raise ValueError('owner producer/radius differs')
  depth=0;receiver=None;admitted=False
  for r in rows:
   event=r.get('event')
   if event=='notify-enter':
    depth+=1;receiver=r['receiver']['identity'];admitted=False
    if r['prevention']!=0:raise ValueError('public prevention differs')
    if receiver==4 and r['ability_flags']&0x4000:raise ValueError('explicit Move enabled acquisition')
   elif event=='admission':admitted=r['accepted']==1
   elif event=='begin':
    if depth!=1 or r['receiver']!=receiver or not admitted:raise ValueError('exemption precedes admission')
   elif event=='notify-exit':depth-=1
   if depth not in (0,1):raise ValueError('unbalanced handler trace')
  if depth:raise ValueError('unterminated handler')
 map=bundle['map']
 if map['map_sha256']!=spec['map_sha256'] or map['fine_origin']!=[0,0] or map['shadow_size']!=4096 or map['probe_template_sha256']!=spec['pins'][SOURCES[2]] or map['target_builder_sha256']!=spec['pins'][SOURCES[3]]:raise ValueError('map provenance differs')
 return dict(captures=2,controls=1,public_markers=30,notifications=40,exemptions=10,range_decisions=36)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=validate(json.loads(FIXTURE.read_text()));live=runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec);original_bytes(a.binary,spec['instructions']);a.report.parent.mkdir(parents=True,exist_ok=True)
 runner.TESTS=TESTS
 result=dict(passed=True,task=spec['task'],**live,instructions=len(spec['instructions']),engine=runner.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
