#!/usr/bin/env python3
"""Verify selected singleton request publication without rewriting prior retail expectations."""
import argparse,gzip,hashlib,json,os,re,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work239-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/work239_probe.j','tools/frida/research/work239_make_map.py',
 'tools/frida/research/work239_observer.js','tools/frida/research/work239b_probe.j',
 'tools/frida/research/work239b_make_map.py','tools/frida/research/work239b_observer.js',
 'tools/frida/research/work228_capture.py','tools/ghidra/research/Work239Evidence.java',
 'tools/ghidra/fixtures/retail-selected-independent-1.27.json',
 'tools/ghidra/fixtures/retail-captain-roster161-1.27.json']
TESTS=['wc3_movement.selected239_singleton_clears_player_request_history',
 'wc3_movement.selected237_ground_and_flight_share_primary_request',
 'wc3_movement.selected238_alt_owns_separate_flight_requests',
 'wc3_movement.selected_independent_shift_inputs_match_original_complete_journeys',
 'wc3_movement.formation170_public_selection_and_independent_orders_match_retail',
 'wc3_bot.captain_large_roster_preserves_admission_batches_and_cold_save']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-01.1' or spec['game_sha256']!=SHA or
    spec['engine_tests']!=TESTS or set(spec['pins'])!=set(SOURCES) or len(spec['instructions'])!=1920):
  raise ValueError('producer contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('changed source '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle differs')
 if spec['expected']!={'singleton_flags':0,'pair_flags':8,'singleton_history':'invalid','pair_members':2,'singleton_members':1}:raise ValueError('original expectations differ')
 return spec

def normalize(rows,transition):
 packets=[r for r in rows if r.get('event')=='packet'];binds=[r for r in rows if r.get('event')=='bind' and r['depth']==0]
 if any(r['words'][7]!=851986 for r in packets):raise ValueError('not Move')
 flags=[r['words'][6]&0xffff for r in packets]
 if transition:
  pubs=[r for r in rows if r.get('event')=='publish'];attachments=[r for r in rows if r.get('event')=='attach'];retains=[r for r in rows if r.get('event')=='retain']
  if len(pubs)!=3 or len(attachments)!=2 or len(retains)!=2:raise ValueError('missing pair/singleton publication')
  invalid=[0xffffffff]*2;history=pubs[0]['after']
  if(history==invalid or pubs[0]['before']!=invalid or pubs[1]['before']!=invalid or pubs[1]['after']!=history or
     pubs[2]['before']!=history or pubs[2]['after']!=invalid or pubs[2]['unit']!=pubs[0]['unit'] or
     pubs[0]['unit']==pubs[1]['unit'] or [p['flags']for p in pubs]!=[8,8,0]):raise ValueError('history transition differs')
  members=[[p['mover']for p in pubs[:2]],[pubs[0]['mover']]]
  if [b['members']for b in binds]!=members:raise ValueError('physical members differ')
 elif any(r.get('event')in('attach','retain','publish')for r in rows):raise ValueError('singleton prepared a shared request')
 if flags!=([8,0]if transition else[0]) or len(binds)!=(2 if transition else 1) or any(b['flags']!=0 for b in binds):raise ValueError('producer flags differ')
 if [len(b['members'])for b in binds]!=([2,1]if transition else[1]):raise ValueError('member count differs')
 return dict(flags=flags,members=[len(b['members'])for b in binds],cleared=transition)

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=4:raise ValueError('missing repeats')
 output=[];marker_count=0
 for i,c in enumerate(captures):
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload'];transition=i>=2
  markers=re.findall(r'call Preload\( "(P239 [^"\r\n]*)" \)',preload)
  ends=[r for r in rows if r.get('event')=='trace-end'];expected=83 if transition else 82
  if(meta.get('mode')!='observe' or meta.get('sha256')!=SHA or not meta.get('owned') or meta['env']not in('B','C') or
     meta['source_sha256']['map']!=spec['map_sha256'][int(transition)] or len(ends)!=1 or not ends[0]['installed'] or
     any(ends[0][k]!=0 for k in('dispatch','depth')) or ends[0]['count']!=(3 if transition else 1) or
     footer.get('event')!='preload-file' or not footer['complete'] or footer['markers']!=expected or
     len(markers)!=expected or markers!=[r['value']for r in rows if r.get('event')=='marker'] or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or
     any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows)):
   raise ValueError('incomplete/invalid capture')
  observer=SOURCES[5 if transition else 2]
  if meta['source_sha256'][Path(observer).name]!=spec['pins'][observer] or meta['source_sha256']['work228_capture.py']!=spec['pins'][SOURCES[6]]:raise ValueError('captured observer differs')
  n=normalize(rows,transition);output.append(n);marker_count+=expected
 if output[0]!=output[1] or output[2]!=output[3]:raise ValueError('repeat differs')
 return dict(captures=4,public_markers=marker_count,history_transitions=2,singleton_flags=0)

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
 original_bytes(a.binary,spec['instructions']);a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**live,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
