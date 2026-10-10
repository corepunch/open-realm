#!/usr/bin/env python3
"""Verify original flyer-field instructions, repeated live grids and engine support/save paths."""
import argparse, gzip, hashlib, json, os, re, struct, subprocess, sys
from pathlib import Path
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes
from research.work228_oracle import original
from research.work228_header import render, HEADER

ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work228-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
SOURCES=['tools/frida/research/'+p for p in ('work228_capture.py','work228_observer.js',
 'work228_make_map.py','map022_make_maps.py','map022_probe.j','map022_observer.js',
 'sep_research_map.py','group032_make_map.py','point214_ui_input.c')]+[
 'tools/frida/make_wc3_pathfinding_map.py','tools/frida/wc3_ui_input.c',
 'tools/ghidra/research/Work228Evidence.java','tools/ghidra/research/work228_oracle.py',
 'tools/ghidra/research/work228_header.py']
bits=lambda x:struct.unpack('<I',struct.pack('<f',x))[0]
flt=lambda x:struct.unpack('<f',struct.pack('<I',x))[0]
f32=lambda x:flt(bits(x))
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(s):return re.findall(r'call Preload\( "(MAP022 [^"\r\n]*)" \)',s)
def sample(g,p):
 w,h=g['width'],g['height'];a=list(map(flt,g['bits']))
 x=f32(f32(f32(p[0]-g['origin'][0])/g['cell'][0])-.5)
 y=f32(f32(f32(p[1]-g['origin'][1])/g['cell'][1])-.5)
 ix,iy=int(x),int(y);x=f32(x-ix);y=f32(y-iy)
 if ix<0:ix,x=0,0
 elif ix>w-2:ix,x=w-2,0
 if iy<0:iy,y=0,0
 elif iy>h-2:iy,y=h-2,0
 nx,ny=f32(1-x),f32(1-y);i=iy*w+ix
 z=f32(f32(f32(a[i+1]*x)*ny)+f32(f32(nx*a[i])*ny))
 z=f32(z+f32(f32(nx*a[i+w])*y))
 return bits(f32(z+f32(f32(a[i+w+1]*x)*y)))

def maximum(data,w,h,r):
 """Literal original candidate order, deliberately independent of the C deque."""
 out=[0]*(w*h)
 for y in range(h):
  row=data[y*w:(y+1)*w];peaks=[];rising=False
  for i in range(1,w):
   if rising:
    if flt(row[i-1])>flt(row[i]):peaks.append(i-1);rising=False
   elif flt(row[i])>flt(row[i-1]):rising=True
  for x in range(w):
   left,right=max(0,x-r),min(w-1,x+r);value=row[left]
   for i in [p for p in peaks if left<p<right]+[right]:
    if flt(row[i])>flt(value):value=row[i]
   out[x*h+y]=value
 return out

def validate(spec):
 if(spec['version']!=1 or spec['task']!='MAP-02.2' or spec['game_sha256']!=SHA or
    set(spec['pins'])!=set(SOURCES) or len(spec['instructions'])!=1090 or
    len(spec['markers'])!=193 or len(spec['kernels']['maximum'])!=32 or len(spec['kernels']['samples'])!=80):
  raise ValueError('flight contract differs')
 for p,sha in spec['pins'].items():
  if digest(ROOT/p)!=sha:raise ValueError('changed source: '+p)
 if digest(BUNDLE)!=spec['bundle_sha256'] or HEADER.read_text()!=render(spec):raise ValueError('changed frozen evidence')
 for c in spec['kernels']['maximum']:
  if maximum(c['input'],c['width'],c['height'],c['radius'])!=c['output']:raise ValueError('original maximum differs')
 for c in spec['kernels']['samples']:
  if sample(spec['kernels']['sample_grid'],list(map(flt,c['point'])))!=c['result']:raise ValueError('original interpolation differs')
 return spec

def validate_runtime(bundle,spec):
 captures=bundle['captures']
 if(len(captures)!=3 or [c['rows'][0]['mode']for c in captures]!=['observe','observe','control'] or
    bundle['prior_markers']!=spec['markers'] or bundle['map']['sha256']!=spec['map_sha256']):
  raise ValueError('missing repeat/control or prior fixture changed')
 stages=[];samples=0
 for c in captures:
  rows,preload=c['rows'],c['preload'];meta,footer=rows[0],rows[-1]
  if(meta['event']!='metadata' or meta['sha256']!=SHA or not meta['owned'] or meta['env']not in ('B','C') or
     meta['source_sha256']['map']!=spec['map_sha256'] or not footer.get('complete') or footer['markers']!=193 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256'] or markers(preload)!=spec['markers'] or
     any(r.get('type')=='error' or r['event']=='trace-failed'for r in rows)):
   raise ValueError('failed, incomplete or changed retail capture')
  for p,sha in spec['pins'].items():
   if p.startswith('tools/frida/') and meta['source_sha256'].get(Path(p).name)!=sha:raise ValueError('capture source differs')
  if meta['mode']=='control':
   if any(r['event']not in ('metadata','owned-loading-input','loading-key','control-start-file','preload-file')for r in rows):raise ValueError('control instrumented')
   continue
  ends=[r for r in rows if r['event']=='trace-end']
  if len(ends)!=1 or ends[0]['installed']is not True:raise ValueError('observer completion missing')
  if [r['value']for r in rows if r['event']=='marker']!=spec['markers']:raise ValueError('live/public disagreement')
  events=['air-initial','air-raise','air-smooth-input','air-radius','air-levels','air-smooth-output']
  stage=[]
  for e in events:
   selected=[r for r in rows if r['event']==e]
   if len(selected)!=1:raise ValueError('missing or repeated field stage')
   stage.append({k:v for k,v in selected[0].items()if k!='ms'})
  stages.append(stage)
  initial,raised,source,radius,levels,completed=stage
  g=source['grid'];w,h=g['width'],g['height']
  if(w!=64 or h!=64 or g!=raised['grid'] or raised['rect']!=[384,512,960,1536] or
     raised['heightBits']!=bits(256) or radius['value']!=6 or levels['value']!=3 or
     completed['grid']!=spec['completed_grid']):raise ValueError('native field producer differs')
  expected=initial['grid']['bits'].copy()
  for y in range(12,31):
   for x in range(16,49):
    vx,vy=(x+2)//4,(y+2)//4
    ground=-(64 if vx in (4,5,11,12)else 192)if vy<=10 and 4<=vx<=12 else 0
    if vy>=12 and vx>=8:ground+=128
    expected[y*64+x]=bits(max(flt(expected[y*64+x]),ground+256))
  if expected!=raised['grid']['bits']:raise ValueError('inclusive initial rectangle differs')
  data=maximum(g['bits'],w,h,radius['value']);data=maximum(data,h,w,radius['value'])
  for _ in range(levels['value']):
   out=[]
   for y in range(h//2):
    for x in range(w//2):
     i=y*2*w+x*2;z=f32(flt(data[i])+0)
     for j in (i+1,i+w,i+w+1):z=f32(z+flt(data[j]))
     out.append(bits(f32(z*.25)))
   data=out;w//=2;h//=2
  if data!=completed['grid']['bits']:raise ValueError('maximum/smoothing order differs')
  pending=None;count=0
  for r in rows:
   if r['event']=='air-sample-input':
    if pending is not None or r['caller']!=0x7434c2:raise ValueError('unpaired sample')
    pending=r['point']
   if r['event']=='air-sample-result':
    if pending is None or sample(completed['grid'],pending)!=r['resultBits']:raise ValueError('live interpolation differs')
    count+=1;pending=None
  if pending is not None or count<200 or count>=5000:raise ValueError('missing or capped samples')
  samples+=count
 if stages[0]!=stages[1]:raise ValueError('field repeat differs')
 return dict(captures=2,controls=1,public_markers=579,live_samples=samples,completed_words=64)

def run_engine(binary,data,report,spec):
 results={}
 for edition in ('classic','tft'):
  log=report.with_name(report.stem+'-'+edition+'.log');junit=log.with_suffix('.xml')
  env=dict(os.environ,TEST_JUNIT=str(junit),LD_LIBRARY_PATH='/GitHub/wc3-analysis/native-sdl2'+os.pathsep+os.environ.get('LD_LIBRARY_PATH',''))
  args=[str(binary),'-data',str(data)]+(['-tft']if edition=='tft'else[])+['+dedicated','1','+test','wc3_support.*']
  with log.open('w')as out:subprocess.run(args,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=180,check=True)
  suite=ET.parse(junit).getroot();names={t.attrib['name']for t in suite.findall('.//testcase')}
  totals=re.findall(r'=== (\d+)/(\d+) assertions passed in (\d+) test\(s\) ===',log.read_text())
  if(len(totals)!=1 or int(totals[0][0])<43000 or totals[0][0]!=totals[0][1] or
     not set(spec['engine_tests'])<=names or any(suite.attrib[k]!='0'for k in ('failures','errors','skipped'))):
   raise ValueError('missing or failed engine support tests')
  results[edition]=dict(tests=len(names),assertions=int(totals[0][0]),log_sha256=digest(log),junit_sha256=digest(junit))
 return results

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--binary',type=Path,required=True)
 ap.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');ap.add_argument('--data',type=Path,default=ROOT/'build/tests');ap.add_argument('--report',type=Path,required=True)
 args=ap.parse_args()
 if args.report.exists():ap.error('report must be new')
 spec=validate(json.loads(FIXTURE.read_text()));proof=validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
 original_bytes(args.binary,spec['instructions'])
 if original(args.binary)!=spec['kernels']:raise ValueError('fresh unchanged-original execution differs')
 args.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**proof,instructions=len(spec['instructions']),engine=run_engine(args.test_binary,args.data,args.report,spec),fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(args.test_binary),limits=spec['limits'],game_library_sha256=digest(args.test_binary.parent.parent/'lib/libgame-wc3-test.so'))
 args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
