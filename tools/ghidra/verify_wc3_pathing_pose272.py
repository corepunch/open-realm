#!/usr/bin/env python3
"""Verify retail bridge/water support, three-tap clamping and published pose."""
import argparse,gzip,hashlib,json,re,struct
from pathlib import Path
from research.pose272_oracle import original,SHA
from research.pose272_header import render,HEADER
from verify_wc3_pathing_target_normalize218 import original_bytes
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-slope272-1.27.json'
BUNDLE=FIXTURE.with_suffix('.json.gz')
SOURCES=['tools/frida/research/pose272_'+s for s in ('probe.j','observer.js','make_map.py')]+[
 'tools/frida/research/rank264_capture.py','tools/ghidra/research/Pose272Evidence.java',
 'tools/ghidra/research/pose272_oracle.py','tools/ghidra/research/pose272_header.py']
TESTS=['wc3_model.*','wc3_support.*','wc3_coordinates.*','wc3_save.*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(s):return re.findall(r'call Preload\( "(P272[^"\r\n]*)" \)',s)
def validate(spec):
 if (spec['version']!=1 or spec['task']!='MOVE-03.3' or spec['game_sha256']!=SHA or
     len(spec['cases'])!=542 or len(spec['markers'])!=59 or len(spec['pathing'])!=4096 or
     len(spec['original']['ground_kernels'])!=512 or set(spec['pins'])!=set(SOURCES)):
  raise ValueError('slope contract differs')
 for p,sha in spec['pins'].items():
  if digest(ROOT/p)!=sha:raise ValueError('changed source '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']or HEADER.read_text()!=render(spec):raise ValueError('changed frozen expectations')
 if digest(ROOT/'tools/ghidra/fixtures/retail-work229-1.27.json')!=spec['prior_fixture_sha256']:raise ValueError('prior expectations changed')
 return spec

def verify_live(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('repeat/control missing')
 poses=[];expected={};published={}
 for c in spec['cases']:expected[(c['type'],*c['point'],c['heading'])]=c
 for i,capture in enumerate(bundle['captures']):
  rows=capture['rows'];meta=rows[0];footer=rows[-1];preload=capture['preload']
  # Generic reused runner keeps its original GROUP-03.2 metadata string.
  if (meta['sha256']!=SHA or not meta['owned']or meta['env']not in ('B','C')or
      meta['mode']!=('control'if i==2 else'observe')or meta['source_sha256']['map']!=spec['map_sha256']or
      meta['source_sha256']['rank264_capture.py']!=spec['pins'][SOURCES[3]]or
      footer['event']!='preload-file'or not footer['complete']or footer['markers']!=59 or
      hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or markers(preload)!=spec['markers']or
      any(r.get('type')=='error'or r['event']=='trace-failed'for r in rows)):raise ValueError('capture provenance/completion differs')
  if i==2:
   if any('seq'in r or r['event']=='trace-end'for r in rows):raise ValueError('control was instrumented')
   continue
  if meta['source_sha256']['pose272_observer.js']!=spec['pins'][SOURCES[1]]:raise ValueError('observer differs')
  events=[r for r in rows if 'seq'in r];ends=[r for r in rows if r['event']=='trace-end']
  if ([r['seq']for r in events]!=list(range(1,len(events)+1))or len(ends)!=1 or not ends[0]['readOnly']or
      not ends[0]['installed']or ends[0]['depth']or ends[0]['count']!=sum(r['event']=='pose'for r in events)or
      [r['value']for r in events if r['event']=='marker']!=spec['markers']or
      [r for r in events if r['event']=='grid']!=[spec['grid']]):raise ValueError('incomplete ordered trace')
  pending=None;observed=set()
  for r in events:
   if r['event']=='pose':
    if pending:raise ValueError('pose overwritten before publication')
    pending=r;poses.append(r)
    if r['caller']!=0x6845f0:raise ValueError('unexpected orientation caller')
    if r['inputs']['pitch'] or r['inputs']['roll']:
     if len(r['samples'])!=3 or len(r['decks'])!=3 or not all(k in r for k in ('angles','unclamped','unclampedMatrix')):raise ValueError('missing sample/clamp evidence')
     for angle,limit in zip(r['angles'][1:],[r['inputs']['pitch'],r['inputs']['roll']]):
      f=lambda v:struct.unpack('<f',struct.pack('<I',v))[0]
      if abs(f(angle))>f(limit):raise ValueError('angle exceeds authored limit')
    elif r['samples'] or r['decks'] or 'angles'in r:raise ValueError('zero-limit fast path changed')
   elif r['event']=='support'and pending:
    p=pending;args=p['inputs'];key=(r['type'],*r['point'],args['heading'])
    c=dict(type=r['type'],point=r['point'],heading=args['heading'],flags=r['flags'],
      limits=[args['pitch'],args['roll']],angles=p.get('angles',[args['heading'],0,0]),matrix=r['matrix'])
    if r['point']!=p['point']or r['matrix']!=p['matrix']:raise ValueError('native transform was not published')
    if key in expected and expected[key]!=c:raise ValueError('repeated published words differ')
    if key in published and published[key]!=c:raise ValueError('retail repeat differs')
    published[key]=c;observed.add(key);pending=None
  if pending or len(observed)!=spec['capture_pose_counts'][i] or (i==0 and not set(expected)<=observed):raise ValueError('crossing coverage differs')
 if not set(expected)<=set(published):raise ValueError('frozen crossing missing')
 return dict(captures=2,controls=1,public_markers=177,poses=len(poses)),poses

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=validate(json.loads(FIXTURE.read_text()));live,poses=verify_live(json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)
 original_bytes(a.binary,spec['instructions'])
 primary=original(a.binary,poses[:542])
 if primary!=spec['original']:raise ValueError('fresh original orientation/ground kernels differ')
 repeated=original(a.binary,poses[542:])
 import verify_wc3_pathing_work242 as engine
 old=engine.TESTS
 try:engine.TESTS=TESTS;checks=engine.run_engine(a.test_binary,a.data,a.report)
 finally:engine.TESTS=old
 result=dict(passed=True,task=spec['task'],**live,ground_kernels=512,original_matrix_words=(primary['matrix_words']+repeated['matrix_words']),engine=checks,fixture_sha256=digest(FIXTURE),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
