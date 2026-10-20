#!/usr/bin/env python3
"""Verify suspension cleanup owners against unchanged public/native retail chronology."""
import argparse,gzip,hashlib,json
from collections import Counter
from pathlib import Path
import verify_wc3_pathing_stop284 as retail
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-scope300-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
TESTS=['wc3_movement.scope300*','wc3_movement.scope299*','wc3_movement.stop284*',
 'wc3_movement.stop285*','wc3_movement.stop247*','wc3_movement.stop257*',
 'wc3_movement.stop258*','wc3_movement.pause*','wc3_order_reentry.*',
 'wc3_order_subscribers.*','wc3_order_lifecycle.*','wc3_point214.*',
 'wc3_interrupt.*','wc3_building.build298*','wc3_save.*',
 'wc3_movement.public_move*','wc3_movement.public_group*','wc3_movement.public_stop*',
 'wc3_movement.public_overlap*','wc3_movement.public_spawn_move*',
 'wc3_movement.public_mover*','wc3_movement.group*']
SOURCES=['tools/frida/research/scope300_observer.js','tools/ghidra/research/Scope300Evidence.java']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def validate_runtime(bundle,old,oldbundle,spec):
 if len(bundle['captures'])!=3 or bundle['captures'][2]!=oldbundle['captures'][2] or bundle['map_metadata']!=oldbundle['map_metadata']:
  raise ValueError('unchanged observer-free control/map required')
 for capture in bundle['captures'][:2]:
  rows=capture['rows'];meta=rows[0];footer=rows[-1];public=retail.markers(capture['preload'])
  if meta.get('mode')!='observe' or meta.get('sha256')!=SHA or not meta.get('owned') or meta.get('env')not in('B','C') or meta['source_sha256']['map']!=old['map_sha256']:
   raise ValueError('original owned capture provenance differs')
  if meta['source_sha256']['scope300_observer.js']!=spec['source_sha256'][SOURCES[0]]:
   raise ValueError('capture observer substituted')
  for p in retail.SOURCES[:4]+retail.SOURCES[-3:]:
   if p.endswith('stop284_observer.js'):continue
   if meta['source_sha256'][Path(p).name]!=old['source_sha256'][p]:raise ValueError('capture input differs '+p)
  if footer.get('event')!='preload-file' or not footer.get('complete') or footer['markers']!=110 or public!=old['public'] or hashlib.sha256(capture['preload'].encode()).hexdigest()!=footer['sha256']:
   raise ValueError('public capture differs')
  native=[r for r in rows if 'seq'in r];ends=[r for r in rows if r.get('event')=='trace-end']
  if [r['seq']for r in native]!=list(range(1218)) or len(ends)!=1 or not ends[0]['installed'] or not ends[0]['readOnly'] or ends[0]['depth'] or ends[0]['counts']!=dict(Counter(r['event']for r in native)):
   raise ValueError('incomplete native chronology')
  if any(r.get('type')=='error' or r.get('event')=='trace-failed'for r in rows):raise ValueError('observer failed')
  cleaned=[{k:v for k,v in r.items()if k not in('fine','proximity')}for r in rows]
  if retail.normalized(cleaned)!=old['chronology'] or retail.projection(cleaned)!=old['projection'] or [r['value']for r in rows if r.get('event')=='marker']!=public:
   raise ValueError('original Stop chronology changed')
  for scene in(5,13):
   inner=[r['fine']for r in rows if r.get('event')=='inner-enter' and r.get('scene')==scene]
   markers=[r['fine']for r in rows if r.get('event')=='marker-spatial' and r.get('scene')==scene]
   if len(inner)!=4 or len(markers)!=5:raise ValueError('missing Pause spatial boundaries')
   identity=inner[0]['identity'];pointer=inner[0]['pointer']
   for fine in inner+markers:
    if fine['identity']!=identity or fine['pointer']!=pointer or fine['category']!=0x010000ca or fine['box']!=[19,19,21,21]:raise ValueError('Pause fine identity/publication differs')
   if any(f['flags']!=2 for f in inner) or any(f['flags']!=0 for f in markers):raise ValueError('Pause exclusion counter differs')
   recoveries=[r['caller']for r in native if r['event']=='unit-stop-enter' and r['scene']==scene]
   if recoveries!=['0x600433']*3+['0x5fc18a']:raise ValueError('Pause cleanup owner differs')
 return dict(captures=2,controls=1,control_reused=True,public_markers=330,native_events=2436,scenes=16,pause_spatial_scenes=4)
def validate(spec):
 if spec['version']!=1 or spec['task']!='MAP-04.2' or spec['game_sha256']!=SHA or spec['engine_tests']!=TESTS:
  raise ValueError('suspension cleanup contract differs')
 if spec['evidence']!='tools/ghidra/fixtures/retail-stop284-1.27.json' or digest(ROOT/spec['evidence'])!=spec['evidence_sha256']:
  raise ValueError('existing original evidence substituted')
 if set(spec['source_sha256'])!=set(SOURCES) or len(spec['instructions'])!=317:raise ValueError('missing Ghidra/source evidence')
 for p,h in spec['source_sha256'].items():
  if digest(ROOT/p)!=h:raise ValueError('source changed '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('capture bundle substituted')
 old=json.loads((ROOT/spec['evidence']).read_text());oldbundle=retail.validate(old)
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,old,oldbundle,spec)
 return old,oldbundle,bundle
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests')
 p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 spec=json.loads(SPEC.read_text());old,oldbundle,bundle=validate(spec)
 original_bytes(a.binary,spec['instructions']);original_bytes(a.binary,old['instructions'])
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:
  runner.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True);engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,status='verified',task=spec['task'],**validate_runtime(bundle,old,oldbundle,spec),instructions=len(spec['instructions']),engine=engine,
  game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),binary_sha256=SHA,limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
