#!/usr/bin/env python3
"""Verify paused native rejection and deferred core Stop against retail."""
import argparse,gzip,hashlib,json,re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-pause286-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
SOURCES=['tools/frida/research/pause286_'+x for x in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/ghidra/research/Pause286Evidence.java','tools/ghidra/research/pause286_oracle.py',
 'tools/ghidra/research/foot03_spatial_harness_copy.py',
 'tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c']
TESTS=['wc3_movement.pause286*','wc3_movement.stop285*','wc3_movement.stop284*',
 'wc3_movement.forced_position*','wc3_movement.stop247*','wc3_movement.stop257*',
 'wc3_movement.stop258*','wc3_order_reentry.*','wc3_order_lifecycle.*','wc3_interrupt.*',
 'wc3_spell.cast283*','wc3_ancient_root.*','wc3_movement.pathing_pause*',
 'wc3_movement.primary_clock_public_move_save_pause_and_stop','wc3_movement.target256_paused*',
 'wc3_patrol_motion.*','wc3_bot.captain*','wc3_movement.public_captain_go_home*',
 'wc3_save.rejects_prior_save_versions','wc3_api.hashtable*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(P286 [^"\r\n]*)" \)',text)
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
def projection(rows):
 result={}
 for r in rows:
  if r.get('event')!='marker':continue
  m=re.fullmatch(r'P286 tick=(\d+) scene=(\d+) label=(\S+) order=(\d+) x=([-\d.]+) y=([-\d.]+)',r['value'])
  if not m:raise ValueError('malformed public marker')
  tick,scene,label,order,x,y=m.groups()
  if int(scene)<14 and (label.startswith(('first-','replace-'))or label in ('waiting','unpause-return','issued-851972','unit-851972','resumed')):
   result.setdefault(scene,[]).append([label,int(order),float(x),float(y)])
 return result
def header(spec):
 h='/* Literal selected public native boundaries from Pause286; full stream retained. */\nstatic struct {char const *label;uint32_t order;float x,y;} const pause286_rows[14][7]={\n'
 for n in range(14):h+='    { '+','.join('{"%s",%u,%.3ff,%.3ff}'%tuple(r)for r in spec['projection'][str(n)])+' },\n'
 return h+'};\n'
def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('two observations and observer-free control required')
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta=rows[0];footer=rows[-1];public=markers(c['preload'])
  if meta.get('mode')!=('control'if i==2 else'observe')or meta.get('sha256')!=SHA or not meta.get('owned')or meta['env']not in('B','C')or meta['source_sha256']['map']!=spec['map_sha256']:raise ValueError('capture provenance differs')
  for p in SOURCES[:4]+SOURCES[-3:]:
   if meta['source_sha256'][Path(p).name]!=spec['source_sha256'][p]:raise ValueError('capture source differs '+p)
  if footer.get('event')!='preload-file'or not footer.get('complete')or footer['markers']!=224 or public!=spec['public']or hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']:raise ValueError('public capture/control differs')
  if any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows):raise ValueError('failed observer')
  native=[r for r in rows if 'seq'in r];ends=[r for r in rows if r.get('event')=='trace-end']
  if i==2:
   if native or ends or any(r.get('event')=='module'for r in rows):raise ValueError('control instrumented')
   continue
  if [r['seq']for r in native]!=list(range(555))or len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['depth']or ends[0]['counts']!=dict(Counter(r['event']for r in native)):raise ValueError('unbalanced/incomplete observer')
  if [r['value']for r in rows if r.get('event')=='marker']!=public or chronology_hash(rows)!=spec['chronology_sha256']or projection(rows)!=spec['projection']:raise ValueError('frozen native chronology differs')
  validation=Counter((r['event'],r.get('result'))for r in native if r['event'].startswith('validate-')and r['event'].endswith('-exit'))
  if validation!=Counter({('validate-point-exit',221):24,('validate-target-exit',221):8,('validate-immediate-exit',221):2,('validate-immediate-exit',0):2}):raise ValueError('paused validation results differ')
  for scene in range(14):
   admissions=[r for r in native if r['event']=='admit-enter'and r['scene']==scene]
   if [r['order']['command']for r in admissions]!=([851973,851972]if scene in(0,7)else[851973]):raise ValueError('ordinary rejected native reached admission')
 return dict(captures=2,controls=1,public_markers=672,native_events=1110,scenes=14,validation_calls=72)
def validate(spec):
 if spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_tests']!=TESTS:raise ValueError('contract differs')
 for p,h in spec['source_sha256'].items():
  if digest(ROOT/p)!=h:raise ValueError('source changed '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle changed')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,spec)
 if (ROOT/'games/warcraft-3/game/tests/fixtures/retail_pause286.h').read_text()!=header(spec):raise ValueError('engine projection differs from retail')
 return bundle
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
 import sys
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import pause286_oracle
 original=pause286_oracle.original(a.binary)
 if len(original)!=80 or original!=bundle['validation']:raise ValueError('original validation order/ABI differs')
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,status='verified',**validate_runtime(bundle,spec),instructions=len(spec['instructions']),original_cases=len(original),engine=engine,binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['scope'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
