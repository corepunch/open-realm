#!/usr/bin/env python3
"""Verify public embedded-start recovery and frozen first routing decisions."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-foot307-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.foot307*', 'wc3_movement.stop247*', 'wc3_movement.stop258*', 'wc3_movement.structure279*', 'wc3_movement.target*', 'wc3_movement.follow*', 'wc3_movement.smart*', 'wc3_order_lifecycle.*', 'wc3_interrupt.*', 'wc3_save.rejects_prior_save_versions']
SOURCES=['tools/frida/research/foot307_'+s for s in('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c',
 'tools/ghidra/research/Foot307Evidence.java','games/warcraft-3/game/skills/fixtures/retail_foot307_motion.h']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(F307 [^"\r\n]*)" \)',text)
def scalar_stream(rows):
 result=[]
 for r in rows:
  if r['event']not in('commit-enter','commit-leave'):continue
  g=r['g'];m=g['members'][0]['m']
  if g['count']!=1 or len(g['members'])!=1 or not m or m['group']!=g['id'] or m['id']!=g['members'][0]['id']:raise ValueError('physical membership')
  result.append([r['scene'],r['c'],r['event'],g['flags'],{k:m[k]for k in('pos','vel','clock','maximum','heading','radius','range','flags')}])
 return result

def motion(rows):
 out=[]
 for scene in range(6):
  rr=[r for r in rows if r.get('scene')==scene];bind=next(r for r in rr if r['event']=='bind');gid=bind['g']['id']
  before=[r for r in rr if r['event']=='commit-enter'and r['g']['id']==gid][:12]
  after=[r for r in rr if r['event']=='commit-leave'and r['g']['id']==gid][:12]
  if len(before)!=len(after) or len(before)!=(3 if scene in(3,4)else 12):raise ValueError('first segment incomplete')
  states=[]
  for a,b in zip(before,after):
   if a['c']!=b['c']:raise ValueError('unpaired commit')
   m=a['g']['members'][0]['m'];end=b['g']['members'][0]['m']
   states.append([a['c'],*end['clock'],*m['pos'],*m['vel'],m['range'],m['heading']])
  out.append(dict(clock=bind['m']['clock'],counter=bind['c'],rows=states,
    fine=next((r for r in rr if r['event']=='fine-leave'),None),
    advance=next((r for r in rr if r['event']=='advance-leave'),None)))
 return out

def validate_runtime(bundle,spec):
 caps=bundle['captures']
 if len(caps)!=3 or [c['rows'][0]['mode']for c in caps]!=['observe','observe','control']:raise ValueError('two repeats and unhooked control')
 streams=[]
 for cap in caps:
  rows=cap['rows'];meta=rows[0];footer=rows[-1];public=markers(cap['preload'])
  if(meta['task']!='FOOT-04.1/foot307' or meta['sha256']!=SHA or not meta['owned'] or
     meta['env']not in('B','C') or meta['source_sha256']['map']!=spec['map_sha256'] or
     footer['event']!='preload-file' or not footer.get('complete') or footer['markers']!=331 or
     len(public)!=331 or public!=spec['public'] or hashlib.sha256(cap['preload'].encode()).hexdigest()!=footer['sha256'] or
     any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows)):raise ValueError('capture provenance/completion/public control')
  for p in SOURCES[:7]:
   if meta['source_sha256'][Path(p).name]!=spec['pins'][p]:raise ValueError('captured source differs '+p)
  if meta['mode']=='control':
   if any(r['event']in('trace-end','bind','fine-enter','commit-enter')for r in rows):raise ValueError('hooked control')
   continue
  ends=[r for r in rows if r['event']=='trace-end'];live=[r for r in rows if 'seq'in r and r['event']!='trace-end']
  if len(ends)!=1 or not ends[0]['installed'] or not ends[0]['readOnly'] or ends[0]['seq']!=len(live) or [r['seq']for r in live]!=list(range(len(live))):raise ValueError('observer completeness')
  if dict(Counter(r['event']for r in live))!=ends[0]['counts'] or len(live)!=2641:raise ValueError('lost observer events')
  if [r['value']for r in live if r['event']=='marker'and ' label=start 'not in r['value']]!=public:raise ValueError('live/preload mismatch')
  if motion(rows)!=spec['motion']:raise ValueError('first route/motion words differ')
  for scene in range(6):
   rr=[r for r in rows if r.get('scene')==scene];admitted=next(r for r in rr if r['event']=='marker'and ' label=accepted 'in r['value'])
   match=re.search(r' x=([\d.]+) y=([\d.]+)',admitted['value'])
   if not match or list(map(float,match.groups()))!=spec['admitted'][scene]:raise ValueError('immediate admitted pose')
   mover_id=next(r for r in rr if r['event']=='bind')['m']['id']
   recovery=next(r for r in rr if r['event']=='recover-leave'and r['m']['id']==mover_id)
   if recovery['result']!=(scene!=0):raise ValueError('owned initial recovery result')
  streams.append(scalar_stream(rows))
 if streams[0]!=streams[1] or len(streams[0])!=1272:raise ValueError('complete scalar stream differs')
 return dict(captures=2,controls=1,public_markers=331,scenes=6,first_states=54,first_route_points=104,repeated_scalar_snapshots=1272)

def validate(spec,bundle=None):
 if(spec['version']!=1 or spec['task']!='FOOT-04.1' or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS or
    len(spec['instructions'])!=663 or set(spec['pins'])!=set(SOURCES)):raise ValueError('embedded-start contract differs')
 for p,h in spec['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source pin '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('frozen capture changed')
 header=(ROOT/SOURCES[-1]).read_text()
 for scene,m in enumerate(spec['motion']):
  for prefix,width,expected in [('motion',9,m['rows']),('route',2,m['fine']['points']if m['fine']else None)]:
   if expected is None:continue
   match=re.search(r'foot307_'+prefix+str(scene)+r'\[\]\['+str(width)+r'\]=\{(.*?)\n\};',header,re.S)
   if not match:raise ValueError('missing literal table')
   words=[int(x,16)for x in re.findall(r'0x([0-9a-f]{8})u',match[1])]
   if [words[i:i+width]for i in range(0,len(words),width)]!=expected:raise ValueError('literal table differs from retail')
 return validate_runtime(bundle if bundle is not None else json.loads(gzip.decompress(BUNDLE.read_bytes())),spec)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());evidence=validate(spec);original_bytes(a.binary,spec['instructions'])
 import verify_wc3_pathing_work242 as engine
 engine.TESTS=TESTS
 result=dict(passed=True,status='verified',task=spec['task'],instructions=len(spec['instructions']),**evidence,
    engine=engine.run_engine(a.test_binary,a.data,a.report),binary_sha256=SHA,
    game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
