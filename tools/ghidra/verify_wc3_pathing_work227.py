#!/usr/bin/env python3
"""Verify original inside-construction suppression phases and production engine inverses."""
import argparse, copy, gzip, hashlib, json, os, re, subprocess
from collections import Counter
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes

ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work227-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCES=['tools/frida/research/work227_'+s for s in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/frida/research/sep_research_map.py','tools/frida/research/group032_make_map.py',
 'tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/Work227Evidence.java']
TEST='wc3_building.inside_construction_owns_counted_suppression_independently_of_pause'
PHASES={'created':0,'inside':10,'paused':16,'resumed':22,'paused_again':26,
        'target_exit':32,'owner_changed':36,'unpaused':42,'move':46}

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(s):return re.findall(r'call Preload\( "(W227 [^"\r\n]*)" \)',s)
def normalize(rows):
 out=[]
 for r in rows:
  if 'seq' not in r or r['event'] in ('module','marker'):continue
  r=copy.deepcopy(r)
  for k in ('before','after','state'):
   if k in r:
    r[k].pop('identity');r[k].pop('moverIdentity')
  out.append(r)
 return out

def phases(rows):
 out=[];state=None
 for r in rows:
  if r['event']=='configuration':state=r
  if r['event']!='marker' or ' label=sample ' not in r['value']:continue
  match=re.search(r'tick=(\d+) scene=(\d+)',r['value'])
  tick,profile=map(int,match.groups())
  for stage,offset in PHASES.items():
   if tick!=profile*70+offset:continue
   if state is None:raise ValueError('configuration missing before phase')
   enabled,selector,category,rank=state['args']
   packed=((selector&15)<<16)|((category&255)<<20)|((rank&15)<<28)
   public=re.search(r'paused=(\d+) hidden=(\d+)',r['value'])
   paused,hidden=map(int,public.groups())
   out.append(dict(profile=profile,stage=stage,enable=enabled,policy=packed if enabled else 0,
       paused=paused,hidden=hidden,depth=state['state']['disableDepth']))
 if len(out)!=72:raise ValueError('missing work phase')
 return out

def validate(spec):
 if(spec['version']!=1 or spec['task']!='SEP-01.2' or spec['game_sha256']!=SHA or
    spec['engine_test']!=TEST or set(spec['pins'])!=set(SOURCES) or
    len(spec['instructions'])!=488 or len(spec['markers'])!=547 or len(spec['events'])!=388):
  raise ValueError('work contract differs')
 for path,expected in spec['pins'].items():
  if digest(ROOT/path)!=expected:raise ValueError('changed source: '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('changed capture bundle')
 return spec

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control']:
  raise ValueError('repeat/control required')
 for c in captures:
  rows,preload=c['rows'],c['preload'];meta,footer=rows[0],rows[-1]
  if(meta['event']!='metadata' or meta['task']!=spec['task'] or meta['sha256']!=SHA or
     not meta['owned'] or meta['env']not in ('B','C') or meta['source_sha256']['map']!=spec['map_sha256'] or
     footer['event']!='preload-file' or not footer.get('complete') or footer['markers']!=547 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or markers(preload)!=spec['markers'] or
     any(r.get('type')=='error' or r['event']=='trace-failed'for r in rows)):
   raise ValueError('failed, incomplete or unowned capture')
  for path in SOURCES[:-1]:
   if meta['source_sha256'][Path(path).name]!=spec['pins'][path]:raise ValueError('captured source differs')
  if any(meta['source_sha256'][k]!=v for k,v in spec['helper_sha256'].items()):raise ValueError('input helper differs')
  if meta['mode']=='control':
   if any('seq'in r or r['event']=='trace-end'for r in rows):raise ValueError('instrumented control')
   continue
  public=[r['value']for r in rows if r['event']=='marker']
  if len(public)!=550 or public[3:]!=spec['markers']:raise ValueError('live/preload disagree')
  ends=[r for r in rows if r['event']=='trace-end']
  if(len(ends)!=1 or ends[0].get('readOnly')is not True or
     dict(Counter(r['event']for r in rows if 'seq'in r))!=ends[0]['counts']):raise ValueError('observer counts differ')
  if normalize(rows)!=spec['events'] or phases(rows)!=spec['phases']:raise ValueError('native work contract differs')
 return dict(captures=2,controls=1,public_markers=1641,native_events=776,work_phases=72)

def validate_engine(log,spec):
 words=re.findall(r'W227 engine profile=(\d+) stage=(\w+) enable=(\d+) policy=([0-9a-f]{8}) paused=(\d+) hidden=(\d+) depth=(-?\d+)',log)
 rows=[dict(profile=int(p),stage=s,enable=int(e),policy=int(w,16),paused=int(pa),hidden=int(h),depth=int(d))for p,s,e,w,pa,h,d in words]
 if rows!=spec['phases']:raise ValueError('engine work phases differ from retail')
 return len(rows)

def run_engine(binary,data,report,spec):
 results={}
 for edition in ('classic','tft'):
  log=report.with_name(report.stem+'-'+edition+'.log');junit=log.with_suffix('.xml')
  env=dict(os.environ,TEST_JUNIT=str(junit),LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
  args=[str(binary),'-data',str(data)]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test',TEST]
  with log.open('w')as out:subprocess.run(args,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=120,check=True)
  suite=ET.parse(junit).getroot();tests=suite.findall('.//testcase')
  totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
  if(len(totals)!=1 or int(totals[0][0])<100 or totals[0][0]!=totals[0][1] or
     [t.attrib['name']for t in tests]!=[TEST] or
     any(suite.attrib[k]!='0'for k in ('failures','errors','skipped'))):raise ValueError('missing or failed engine work test')
  results[edition]=dict(work_phases=validate_engine(log.read_text(),spec),tests=1,assertions=int(totals[0][0]),
    log_sha256=digest(log),junit_sha256=digest(junit))
 return results

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--binary',type=Path,required=True)
 ap.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
 ap.add_argument('--data',type=Path,default=ROOT/'build/tests');ap.add_argument('--report',type=Path,required=True)
 args=ap.parse_args();spec=validate(json.loads(FIXTURE.read_text()))
 if args.report.exists():ap.error('report must be new')
 proof=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
 original_bytes(args.binary,spec['instructions']);args.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**proof,instructions=len(spec['instructions']),
  engine=run_engine(args.test_binary,args.data,args.report,spec),fixture_sha256=digest(FIXTURE),
  bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(args.test_binary),
  limits=spec['limits'],game_library_sha256=digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'))
 args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
