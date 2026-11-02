#!/usr/bin/env python3
"""Verify original pause stages and the core suspension/reentrant point contract."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-pause287-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
SCENES=[0,1,4,5,7,13,15,16,17,18]
SOURCES=['tools/frida/research/pause287_'+x for x in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/ghidra/research/Pause287Evidence.java','tools/ghidra/research/pause287_oracle.py',
 'tools/ghidra/research/foot03_spatial_harness_copy.py',
 'tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c']
TESTS=['wc3_movement.pause287*','wc3_movement.pause286*','wc3_movement.stop285*','wc3_movement.stop284*',
 'wc3_movement.forced_position*','wc3_movement.stop247*','wc3_movement.stop257*',
 'wc3_movement.stop258*','wc3_order_reentry.*','wc3_order_lifecycle.*','wc3_interrupt.*',
 'wc3_spell.cast283*','wc3_ancient_root.*','wc3_movement.pathing_pause*',
 'wc3_movement.primary_clock_public_move_save_pause_and_stop','wc3_movement.target256_paused*',
 'wc3_patrol_motion.*','wc3_bot.captain*','wc3_movement.public_captain_go_home*',
 'wc3_save.rejects_prior_save_versions','wc3_api.hashtable*',
 'wc3_building.repair_work_separation_follows_primary_owner_and_saved_inverse',
 'wc3_movement.repulsion_owner_change_pause_and_removal','wc3_repulsion_policy.*','wc3_repulsion_overlap.*',
 'wc3_movement.target222*','wc3_movement.target223*',
 'wc3_unit.unravenform_snaps_new_animation_frame_while_unit_is_paused',
 'wc3_unit.raven_morph_completion_and_takeoff_survive_move_order']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(P287 [^"\r\n]*)" \)',text)
def normalized(rows):
 identities={}
 def visit(o,key=None):
  if isinstance(o,dict):return {k:visit(v,k)for k,v in o.items()}
  if isinstance(o,list):
   if key in ('identity','head','tail','task'):
    pair=tuple(o)
    if pair==(0xffffffff,0xffffffff):return o
    return ['id',identities.setdefault(pair,len(identities))]
   return [visit(v)for v in o]
  return o
 return [visit(r)for r in rows if 'seq'in r or r.get('event')=='marker']
def chronology_hash(rows):return hashlib.sha256(json.dumps(normalized(rows),sort_keys=True,separators=(',',':')).encode()).hexdigest()
def projection(public):
 result={str(n):[]for n in SCENES}
 for s in public:
  m=re.fullmatch(r'P287 tick=(\d+) scene=(\d+) label=(\S+) order=(\d+) paused=([01]) x=([-\d.]+) y=([-\d.]+) px=([-\d.]+) py=([-\d.]+) target=([01]) defend=(\d+) speed=([-\d.]+)',s)
  if not m:raise ValueError('malformed public marker')
  tick,scene,label,order,paused,x,y,px,py,target,defend,speed=m.groups()
  if int(scene)not in SCENES or label in ('selector-0','start','scene-start','settled','complete')or label.endswith('-852056'):continue
  result[scene].append([label,int(order),int(paused),float(px),float(py),int(target)])
 return result
def header(spec):
 h='/* Frozen public pause/resume projection. Position integration and modal off are separate owners. */\n'
 h+='static struct {char const *label;uint32_t order;bool paused;float px,py;bool target;} const pause287_rows[20][20]={\n'
 for n in SCENES:h+='    [%u]={ '%n+','.join('{"%s",%u,%u,%.3ff,%.3ff,%u}'%tuple(r)for r in spec['projection'][str(n)])+' },\n'
 return h+'};\n'
def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('two observations and observer-free control required')
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta=rows[0];footer=rows[-1];public=markers(c['preload'])
  if meta.get('mode')!=('control'if i==2 else'observe')or meta.get('sha256')!=SHA or not meta.get('owned')or meta['env']not in('B','C')or meta['source_sha256']['map']!=spec['map_sha256']:raise ValueError('capture provenance differs')
  for p in SOURCES[:4]+SOURCES[-3:]:
   if meta['source_sha256'][Path(p).name]!=spec['source_sha256'][p]:raise ValueError('captured source differs '+p)
  if footer.get('event')!='preload-file'or not footer.get('complete')or footer['markers']!=285 or public!=spec['public']or hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']:raise ValueError('public capture/control differs')
  if any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows):raise ValueError('failed observer')
  native=[r for r in rows if 'seq'in r];ends=[r for r in rows if r.get('event')=='trace-end']
  if i==2:
   if native or ends or any(r.get('event')=='module'for r in rows):raise ValueError('control instrumented')
   continue
  if [r['seq']for r in native]!=list(range(815))or len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['depth']or ends[0]['counts']!=dict(Counter(r['event']for r in native)):raise ValueError('unbalanced/incomplete observer')
  if [r['value']for r in rows if r.get('event')=='marker']!=public or chronology_hash(rows)!=spec['chronology_sha256']or projection(public)!=spec['projection']:raise ValueError('frozen native chronology differs')
 return dict(captures=2,controls=1,public_markers=855,native_events=1630,captured_scenes=20,engine_projection_scenes=len(SCENES))
def validate(spec):
 if spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_tests']!=TESTS:raise ValueError('contract differs')
 for p,h in spec['source_sha256'].items():
  if digest(ROOT/p)!=h:raise ValueError('source changed '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle changed')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,spec)
 if (ROOT/'games/warcraft-3/game/tests/fixtures/retail_pause287.h').read_text()!=header(spec):raise ValueError('engine projection differs from retail')
 if digest(ROOT/'games/warcraft-3/game/tests/fixtures/retail_pause287_probe.h')!=spec['engine_probe_sha256']:raise ValueError('engine probe changed')
 return bundle
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
 import sys
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import pause287_oracle
 original=pause287_oracle.original(a.binary)
 if len(original)!=100 or original!=bundle['stages']:raise ValueError('original stage store/call order differs')
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,status='verified',**validate_runtime(bundle,spec),instructions=len(spec['instructions']),original_cases=len(original),engine=engine,binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['scope'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
