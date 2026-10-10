#!/usr/bin/env python3
"""Verify original mesh instructions, repeated retail captures and authoritative engine support."""
import argparse,gzip,hashlib,json,os,re,struct,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes
from research.work229_oracle import original,SHA
from research.work229_header import render,HEADER
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work229-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/work229_observer.js','tools/frida/research/work228_capture.py',
 'tools/ghidra/research/Work229Evidence.java','tools/ghidra/research/work229_oracle.py','tools/ghidra/research/work229_header.py']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(preload):return re.findall(r'call Preload\( "(MAP022 [^"\r\n]*)" \)',preload)
def queries(rows):
 on=False;out={}
 for r in rows:
  if r.get('event')=='marker'and 'tick=0 label=start' in r['value']:on=True
  if on and r.get('event')=='deck':
   v=dict(point=r['point'],hit=r['hit'],height=r['height']if r['hit']else 0);key=tuple(v['point'])
   if key in out and out[key]!=v:raise ValueError('unstable live query')
   out[key]=v
 return list(out.values())
def validate(spec):
 if(spec['version']!=1 or spec['task']!='MAP-02.2' or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES) or len(spec['kernels']['cases'])!=1837 or
    len(spec['kernels']['transforms'])!=512 or len(spec['kernels']['distances'])!=518 or
    len(spec['queries'])!=383 or len(spec['mesh']['vertices'])!=384 or len(spec['mesh']['indices'])!=192 or
    spec['mesh']['primitive']!=3 or len(spec['markers'])!=193):raise ValueError('mesh contract differs')
 prior=ROOT/'tools/ghidra/fixtures/research/MAP-02.2-expected.json'
 if digest(prior)!=spec['prior_fixture_sha256']:raise ValueError('prior retail expectations changed')
 expected=json.loads(prior.read_text())
 if len(spec['support_cases'])!=40 or len(spec['terrain'])!=4096:raise ValueError('incomplete combined fixture')
 for c in spec['support_cases']:
  old=expected['support']['authored'][c['key']];kind,name=c['key'].split('@')
  if(c['type']!=kind or c['point']!=[x*32 for x in expected['cells'][name]['fine']]or
     c['height']!=int(old['z_bits'],16)or c['deck']!=old['on_bridge']or c['deep']!=old['deep_flag']):raise ValueError('combined fixture differs from prior retail expectations')
 wpm=b'MP3W'+struct.pack('<III',0,64,64)+bytes(spec['terrain'])
 if hashlib.sha256(wpm).hexdigest()!=spec['initial_fixture_wpm_sha256']:raise ValueError('authored WPM differs')
 for p,sha in spec['pins'].items():
  if digest(ROOT/p)!=sha:raise ValueError('changed source: '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']or HEADER.read_text()!=render(spec):raise ValueError('changed frozen evidence')
 return spec

def validate_runtime(bundle,spec):
 captures=bundle['captures'];live=0
 if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:raise ValueError('missing repeats/control')
 union={}
 for c in captures:
  rows=c['rows'];meta=rows[0];footer=rows[-1];preload=c['preload']
  if(meta['sha256']!=SHA or not meta['owned']or meta['env']not in ('B','C')or
     meta['source_sha256']['map']!=spec['map_sha256']or not footer.get('complete')or footer['markers']!=193 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or markers(preload)!=spec['markers']or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('bad native capture')
  if meta['mode']=='observe':
   if(meta['source_sha256']['work229_observer.js']!=spec['pins']['tools/frida/research/work229_observer.js']or
      [r['value']for r in rows if r.get('event')=='marker']!=spec['markers']):raise ValueError('observer inputs/markers differ')
   ends=[r for r in rows if r.get('event')=='trace-end']
   if len(ends)!=1 or ends[0]['groups']!=2 or ends[0]['queries']<3000:raise ValueError('incomplete observer trace')
   groups=[r for r in rows if r.get('event')=='mesh-group']
   if groups!=[spec['early_mesh'],spec['mesh']]:raise ValueError('live mesh differs')
   for q in queries(rows):
    key=tuple(q['point'])
    if key in union and union[key]!=q:raise ValueError('repeat differs')
    union[key]=q;live+=1
 if list(union.values())!=spec['queries']:raise ValueError('live query union differs')
 return dict(captures=2,controls=1,public_markers=579,live_queries=live,unique_queries=len(union))

def run_engine(binary,data,report,spec):
 out={}
 for edition in ('classic','tft'):
  log=report.with_name(report.stem+'-'+edition+'.log');junit=log.with_suffix('.xml')
  env=dict(os.environ,TEST_JUNIT=str(junit),LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
  args=[str(binary),'-data',str(data)]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test','wc3_model.*']
  with log.open('w')as f:subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180,check=True)
  suite=ET.parse(junit).getroot();names={t.attrib['name']for t in suite.findall('.//testcase')}
  totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
  if(len(totals)!=1 or int(totals[0][0])<24000 or totals[0][0]!=totals[0][1]or
     not set(spec['engine_tests'])<=names or any(suite.attrib[k]!='0'for k in ('failures','errors','skipped'))):raise ValueError('missing/failed model tests')
  out[edition]=dict(tests=len(names),assertions=int(totals[0][0]),log_sha256=digest(log),junit_sha256=digest(junit))
 return out

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=validate(json.loads(FIXTURE.read_text()));proof=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
 original_bytes(a.binary,spec['instructions'])
 if original(a.binary)!=spec['kernels']:raise ValueError('fresh original mesh execution differs')
 a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**proof,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report,spec),fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
