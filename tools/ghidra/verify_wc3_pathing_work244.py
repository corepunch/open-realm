#!/usr/bin/env python3
"""Certify file-backed footprint decoding and complete mixed Alt+Shift journeys."""
import argparse,gzip,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
from research.work244_texture_oracle import original
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-work244-1.27.json';BUNDLE=FIXTURE.with_suffix('.json.gz')
MOTION=ROOT/'games/warcraft-3/game/tests/fixtures/retail_selected_motion244.h'
TEXTURE=ROOT/'games/warcraft-3/game/tests/fixtures/retail_path_texture244.h'
SOURCES=['tools/frida/research/work244_observer.js','tools/frida/research/work228_capture.py',
 'tools/frida/research/work243_probe.j','tools/frida/research/work243_make_map.py',
 'tools/ghidra/research/Work244Evidence.java','tools/ghidra/research/work244_texture_oracle.py']
TESTS=['wc3_movement.selected244*','wc3_collision.*','wc3_destructable.*',
 'wc3_movement.rectangular_bridge_support_bounds_follow_quarter_turns','wc3_fine_spatial.*',
 'wc3_movement.selected243*','wc3_save.mixed_sparse_regions_reload_in_owner_order_and_retain_inverse_pixels']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def trajectory(rows):
 units={tuple(r['mover']):i for i,r in enumerate(r for r in rows if r.get('event')=='attach')}
 packet=next(r for r in rows if r.get('event')=='packet')
 return [packet['clock'][0],packet['counter'],*packet['words'][10:12]],[[units[tuple(r['after']['id'])],r['after']['clock'][0],*r['after']['pose'][:4],r['after']['pose'][5]] for r in rows if r.get('event')=='motion244']
def headers(bundle,kernel):
 inputs,motions=zip(*(trajectory(c['rows'])for c in bundle['captures'][:2]))
 row=lambda a:'{'+','.join(f'0x{v:08x}u'for v in a)+'}'
 out=['/* Frozen original commits from complete Work243 mixed Alt+Shift runs. */','#ifndef RETAIL_SELECTED_MOTION244_H','#define RETAIL_SELECTED_MOTION244_H',
 'static uint32_t const selected244_inputs[2][4]={'+','.join(map(row,inputs))+'};','static uint32_t const selected244_motion[2][850][7]={']
 for motion in motions:out+=['{',',\n'.join(map(row,motion)),'},']
 out+=['};','#endif']
 rows=bundle['captures'][2]['rows'];hierarchy=next(r for r in rows if r.get('event')=='hierarchy244');hashes=[]
 for m in hierarchy['maps']:
  lanes=[]
  for shift in(6,0,2,4):
   h=14695981039346656037
   for word in m['words']:h=((h^((word>>(24+shift))&3))*1099511628211)&0xffffffffffffffff
   lanes.append(h)
  hashes.append(lanes)
 cats=next(r['categories']for r in rows if r.get('event')=='texture244')
 texture=['#ifndef RETAIL_PATH_TEXTURE244_H','#define RETAIL_PATH_TEXTURE244_H',
 '/* Original21e790 loop outputs and read-only LT06 capture14, never engine-generated. */',
 'static uint64_t const selected244_hierarchy[4][4]={'+','.join('{'+','.join('UINT64_C(%d)'%h for h in hs)+'}'for hs in hashes)+'};',
 'static uint8_t const selected244_categories[576]={'+','.join(map(str,cats))+'};',
 'static uint8_t const texture244_colors[216][3]={'+','.join('{'+','.join(map(str,v))+'}'for v in kernel['bgra'])+'};',
 'static uint8_t const texture244_categories[216]={'+','.join(map(str,kernel['categories']))+'};','#endif']
 return '\n'.join(out)+'\n','\n'.join(texture)+'\n'
def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=4:raise ValueError('missing motion/texture repeats')
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta,footer=rows[0],rows[-1];preload=c['preload'];ends=[r for r in rows if r.get('event')=='trace-end']
  if(meta['sha256']!=SHA or not meta.get('owned')or meta['env']not in('B','C')or meta['mode']!='observe' or
     meta['source_sha256']['map']!=spec['map_sha256']or not footer.get('complete')or footer['markers']!=242 or
     hashlib.sha256(preload.encode()).hexdigest()!=footer['sha256']or
     [r['value']for r in rows if r.get('event')=='marker']!=re.findall(r'call Preload\( "(P243 [^"\r\n]*)" \)',preload)or
     len(ends)!=1 or not ends[0]['installed']or ends[0]['dispatch']or ends[0]['depth']or ends[0]['count']!=5 or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('invalid capture')
  observer=meta['source_sha256']['work244_observer.js']
  expected=spec['historical_observer_sha256']if i<2 else spec['pins'][SOURCES[0]]
  if observer!=expected or meta['source_sha256']['work228_capture.py']!=spec['pins'][SOURCES[1]]:raise ValueError('observer provenance differs')
  inputs=[r for r in rows if r.get('event')=='player-input']
  if len(inputs)!=1 or inputs[0]['rc']or not inputs[0]['plan']['alt']or not inputs[0]['plan']['shift']or [inputs[0]['plan']]!=meta['input']:raise ValueError('missing real Alt+Shift input')
  packet,motion=trajectory(rows)
  if len(motion)!=850 or len([r for r in rows if r.get('event')=='visit244'])!=855:raise ValueError('incomplete journey')
  if hashlib.sha256(json.dumps([packet,motion],separators=(',',':')).encode()).hexdigest()!=spec['journey_sha256'][i]:raise ValueError('retail journey changed')
  if i>=2:
   textures=[r for r in rows if r.get('event')=='texture244']
   if len(textures)!=2 or any(t!=spec['texture']for t in textures):raise ValueError('decoded texture differs')
   hierarchy=next(r for r in rows if r.get('event')=='hierarchy244')
   if hierarchy['maps']!=spec['hierarchy']:raise ValueError('retail hierarchy differs')
 return dict(captures=4,public_markers=968,motion_commits=3400,owner_visits=3420,decoded_pixels=576)
def run_engine(binary,data,report):
 import verify_wc3_pathing_work242 as prior
 old=prior.TESTS
 try:prior.TESTS=TESTS;return prior.run_engine(binary,data,report)
 finally:prior.TESTS=old
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(FIXTURE.read_text())
 if spec['version']!=1 or spec['task']!='GROUP-04.6' or spec['game_sha256']!=SHA or set(spec['pins'])!=set(SOURCES)or spec['engine_tests']!=TESTS:raise ValueError('contract changed')
 for path,sha in spec['pins'].items():
  if digest(ROOT/path)!=sha:raise ValueError('changed source '+path)
 for path,sha in spec['assets'].items():
  if digest(ROOT/path)!=sha:raise ValueError('changed asset '+path)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));live=validate_runtime(bundle,spec);original_bytes(a.binary,spec['instructions'])
 kernel=original(a.binary)
 if kernel!=spec['category_kernel']:raise ValueError('original category execution changed')
 motion,texture=headers(bundle,kernel)
 if motion!=MOTION.read_text()or texture!=TEXTURE.read_text():raise ValueError('retail fixture changed')
 a.report.parent.mkdir(parents=True,exist_ok=True)
 result=dict(passed=True,task=spec['task'],**live,category_cases=216,instructions=len(spec['instructions']),engine=run_engine(a.test_binary,a.data,a.report),fixture_sha256=digest(FIXTURE),bundle_sha256=digest(BUNDLE),binary_sha256=SHA,test_binary_sha256=digest(a.test_binary),game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits']);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
