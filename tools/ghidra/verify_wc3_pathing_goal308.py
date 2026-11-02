#!/usr/bin/env python3
"""Verify retail goal lifetimes and public unit placement clamp ordering."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-goal308-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.goal308*','wc3_movement.place308*','wc3_movement.foot307*','wc3_movement.stop247*','wc3_movement.stop258*','wc3_movement.target*','wc3_movement.follow*','wc3_movement.smart*','wc3_movement.retail_gate_blocked_and_outside*','wc3_movement.public_move_matches_original*','wc3_order_lifecycle.*','wc3_interrupt.*','wc3_save.rejects_prior_save_versions']
SOURCES=['tools/frida/research/'+p+'_'+s for p in('goal308','place308')for s in('capture.py','observer.js','make_map.py','probe.j')]+['tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c','tools/ghidra/research/Goal308Evidence.java','games/warcraft-3/game/skills/fixtures/retail_goal308_motion.h']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text,prefix):return re.findall(r'call Preload\( "('+prefix+r' [^"\r\n]*)" \)',text)
def normalize(value):
 if isinstance(value,list):return [normalize(x)for x in value]
 if isinstance(value,dict):return {k:normalize(v)for k,v in value.items()}
 if isinstance(value,str)and re.fullmatch('0x[0-9a-f]+',value):return value!='0x0'
 return value

def capture_set(caps,part,spec,prefix):
 if len(caps)!=3 or [c['rows'][0]['mode']for c in caps]!=['observe','observe','control']:raise ValueError('two repeats and unhooked control required')
 streams=[]
 for cap in caps:
  rows=cap['rows'];meta=rows[0];footer=rows[-1];public=markers(cap['preload'],prefix)
  if(meta['task']!='FOOT-04.2/'+part or meta['sha256']!=SHA or not meta['owned']or meta['env']not in('B','C')or meta['source_sha256']['map']!=spec['maps'][part]or footer['event']!='preload-file'or not footer.get('complete')or public!=spec['public'][part]or footer['markers']!=len(public)or hashlib.sha256(cap['preload'].encode()).hexdigest()!=footer['sha256']or any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('capture provenance, completion or public control')
  for p in SOURCES[:11]:
   if (part+'_'in p or p in SOURCES[8:11])and meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs '+p)
  if meta['mode']=='control':
   if any('seq'in r or r['event']=='trace-end'for r in rows):raise ValueError('hooked control')
   continue
  ends=[r for r in rows if r['event']=='trace-end'];live=[r for r in rows if 'seq'in r and r['event']!='trace-end']
  if len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['seq']!=len(live)or [r['seq']for r in live]!=list(range(len(live)))or dict(Counter(r['event']for r in live))!=ends[0]['counts']:raise ValueError('lost observer events')
  if [r['value']for r in live if r['event']=='marker'and'label=start'not in r['value']]!=public:raise ValueError('live/preload mismatch')
  streams.append(normalize(live))
 if streams[0]!=streams[1]:raise ValueError('repeated native streams differ')
 return len(streams[0])

def goal_motion(rows):
 out=[]
 for scene in range(12):
  rr=[r for r in rows if r.get('scene')==scene];bind=next(r for r in rr if r['event']=='bind');mid=bind['m']['id']
  before=[r for r in rr if r['event']=='commit-enter'and r['g']['count']==1 and r['g']['members'][0]['id']==mid]
  after=[r for r in rr if r['event']=='commit-leave'and r['g']['count']==1 and r['g']['members'][0]['id']==mid]
  if len(before)!=len(after):raise ValueError('unpaired commits')
  states=[]
  for a,b in zip(before,after):
   if a['c']!=b['c']:raise ValueError('unpaired counter')
   m=a['g']['members'][0]['m'];end=b['g']['members'][0]['m']
   states.append([a['c'],*end['clock'],*m['pos'],*m['vel'],m['range'],m['heading'],*end['pos'],*end['vel'],end['heading'],a['tick'],a['g']['flags']])
  fine=next(r for r in rr if r['event']=='fine-leave')
  removal=next((r for r in rr if r['event']=='marker'and'label=before-removal'in r['value']),None)
  out.append(dict(clock=bind['m']['clock'],counter=bind['c'],rows=states,fine={k:fine[k]for k in('result','points','index','flags')},removal=removal,removal_index=sum(r['seq']<removal['seq']for r in before)if removal else 0))
 return out

def validate(spec,bundle=None):
 if(spec['version']!=1 or spec['task']!='FOOT-04.2'or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or len(spec['instructions'])!=920 or set(spec['pins'])!=set(SOURCES)):raise ValueError('goal contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source pin '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('frozen capture changed')
 bundle=bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes()))
 events={part:capture_set(bundle[part],part,spec,prefix)for part,prefix in(('goal308','G308'),('place308','P308'))}
 if goal_motion(bundle['goal308'][0]['rows'])!=spec['motion']:raise ValueError('frozen motion differs')
 header=(ROOT/SOURCES[-1]).read_text()
 for scene,m in enumerate(spec['motion']):
  for prefix,width,expected in [('motion',16,m['rows']),('route',2,m['fine']['points'])]:
   match=re.search(r'goal308_'+prefix+str(scene)+r'\[\]\['+str(width)+r'\]=\{(.*?)\n\};',header,re.S)
   if not match:raise ValueError('missing literal table')
   words=[int(x,16)for x in re.findall(r'0x([0-9a-f]{8})u',match[1])]
   if [words[i:i+width]for i in range(0,len(words),width)]!=expected:raise ValueError('literal table differs from retail')
 rows=bundle['place308'][0]['rows']
 for scene in range(12):
  rr=[r for r in rows if r.get('scene')==scene];writes=[r['point']for r in rr if r['event']=='position-leave']
  if len(writes)!=2 or writes[-1]!=spec['placement'][scene]:raise ValueError('public placement words differ')
  if [r['point']for r in rr if r['event']=='clamp-leave'][-1]!=spec['clamped'][scene]:raise ValueError('outer clamp words differ')
 return dict(captures=4,controls=2,events=events,public_markers={k:len(v)for k,v in spec['public'].items()},motion_states=sum(len(m['rows'])for m in spec['motion']),first_route_points=sum(len(m['fine']['points'])for m in spec['motion']))

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());evidence=validate(spec);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS
 result=dict(passed=True,status='verified',task=spec['task'],instructions=len(spec['instructions']),**evidence,engine=engine.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
