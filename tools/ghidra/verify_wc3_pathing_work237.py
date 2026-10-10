#!/usr/bin/env python3
"""Verify ordinary mixed-flight selected request affinity through original attachment code."""
import argparse,gzip,hashlib,json,os,re,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from research.work237_oracle import original
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work237-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/work237_observer.js','tools/frida/research/work228_capture.py',
 'tools/frida/research/work237_probe.j','tools/frida/research/work237_make_map.py',
 'tools/ghidra/research/work237_oracle.py','tools/ghidra/research/Work237Evidence.java',
 'tools/ghidra/research/foot03_rig.py','tools/ghidra/research/foot03_spatial_harness_copy.py',
 'tools/ghidra/verify_wc3_pathing_target_normalize218.py']
TESTS=['wc3_movement.selected237_ground_and_flight_share_primary_request',
 'wc3_movement.selected_point_move_owns_shared_physical_group',
 'wc3_movement.selected_shift_retains_common_point_and_request_context',
 'wc3_movement.partition235_public_order_partitions_and_saves_physical_owners']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate(spec):
 if(spec['version']!=1 or spec['task']!='GROUP-04.6' or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES) or len(spec['kernel']['rows'])!=288 or spec['engine_tests']!=TESTS):raise ValueError('queued contract differs')
 for path,sha in spec['pins'].items():
  if digest(ROOT/path)!=sha:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 return spec

def normalize(rows):
 attachments=[r for r in rows if r.get('event')=='attach'];retains=[r for r in rows if r.get('event')=='retain'];binds=[r for r in rows if r.get('event')=='bind'];packets=[r for r in rows if r.get('event')=='packet']
 if len(attachments)!=4 or len(retains)!=4 or len(binds)!=4 or len(packets)!=1:raise ValueError('missing selected attachment/binding')
 ids={tuple(r['mover']):i for i,r in enumerate(attachments)};units={r['unit']:i for i,r in enumerate(attachments)}
 requests=attachments[0]['requests'];canonical={}
 if len(ids)!=4 or len(units)!=4 or len(requests)!=3:raise ValueError('aliased selection')
 out=[]
 for r in retains:
  i=units[r['unit']];wrapper=int(r['wrapper'],16)
  if wrapper not in requests:raise ValueError('unknown wrapper')
  slot=requests.index(wrapper);canonical[tuple(r['request'])]=slot
  if attachments[i]['requests']!=requests:raise ValueError('request context changed')
  out.append(dict(index=i,type=attachments[i]['type'],flight=bool(attachments[i]['flags']&0x20000000),forced=attachments[i]['forced'],option=attachments[i]['options'],request=slot))
 groups=[]
 for r in binds:
  if r['depth']==0:groups.append(dict(request=canonical[tuple(r['request'])],flags=r['flags'],members=[ids[tuple(m)]for m in r['members']]))
 return dict(flags=packets[0]['words'][6]&0xffff,order=packets[0]['words'][7],attachments=out,groups=groups)

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=4:raise ValueError('missing ordinary/Alt repeat')
 for i,c in enumerate(captures):
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload']
  markers=re.findall(r'call Preload\( "(P237 [^"\r\n]*)" \)',preload)
  if(meta['mode']!='observe' or meta['sha256']!=SHA or not meta['owned'] or meta['env']not in('B','C') or
     meta['source_sha256']['map']!=spec['map_sha256'] or footer.get('event')!='preload-file' or
     not footer.get('complete') or footer['markers']!=82 or len(markers)!=82 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or
     markers!=[r['value']for r in rows if r.get('event')=='marker'] or
     hashlib.sha256(json.dumps(markers).encode()).hexdigest()!=spec['marker_sha256'][i] or
     any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows)):raise ValueError('bad capture')
  for name in SOURCES[:2]:
   if meta['source_sha256'][Path(name).name]!=spec['pins'][name]:raise ValueError('captured source differs')
  ends=[r for r in rows if r.get('event')=='trace-end'];inputs=[r for r in rows if r.get('event')=='player-input']
  if(len(ends)!=1 or not ends[0]['installed'] or ends[0]['count']!=4 or ends[0]['dispatch'] or ends[0]['depth'] or
     len(inputs)!=1 or inputs[0]['rc']!=0 or inputs[0]['plan']!=spec['inputs'][i//2] or
     normalize(rows)!=spec['live_affinity'][i//2]):raise ValueError('selected affinity differs')
 return dict(captures=4,ordinary_cohorts=2,alt_cohorts=4,public_markers=328)

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
 result=dict(passed=True,task=spec['task'],**proof,complete_scopes=288,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
