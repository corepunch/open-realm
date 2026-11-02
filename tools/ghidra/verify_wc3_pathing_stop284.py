#!/usr/bin/env python3
"""Verify Stop retirement/notification/new-task chronology against frozen retail."""
import argparse, gzip, hashlib, json, re
from collections import Counter
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes, SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-stop284-1.27.json'
BUNDLE=SPEC.with_suffix('.json.gz')
SOURCES=['tools/frida/research/stop284_'+x for x in ('capture.py','observer.js','make_map.py','probe.j')]+[
 'tools/frida/research/group032_make_map.py','tools/frida/make_wc3_pathfinding_map.py',
 'tools/ghidra/research/Stop284Evidence.java','tools/ghidra/research/stop284_oracle.py',
 'tools/ghidra/research/foot03_spatial_harness_copy.py',
 'tools/frida/research/smart277_ui_input.c','tools/frida/research/point214_ui_input.c','tools/frida/wc3_ui_input.c']
TESTS=['wc3_movement.stop284*','wc3_movement.stop247*','wc3_movement.stop257*','wc3_movement.stop258*',
 'wc3_order_reentry.*','wc3_order_lifecycle.*','wc3_interrupt.*','wc3_spell.cast283*',
 'wc3_ancient_root.*','wc3_movement.public_stop*','wc3_save.rejects_prior_save_versions']

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def markers(text):return re.findall(r'call Preload\( "(S284 [^"\r\n]*)" \)',text)
def normalized(rows):
 return [{k:v for k,v in r.items()if k not in ('unit','ability','identity')}
         for r in rows if 'seq' in r or r.get('event')=='marker']
def projection(rows):
 result={};scene=-1;count=0
 for r in rows:
  if r.get('event')=='marker':
   m=re.fullmatch(r'S284 tick=(\d+) scene=(\d+) label=(\S+) order=(\d+) x=([-\d.]+) y=([-\d.]+)',r['value'])
   if not m:raise ValueError('malformed marker')
   tick,n,label,order,x,y=m.groups();n=int(n)
   if n!=scene:scene=n;count=0
   if n<16 and label not in ('start','settled'):
    result.setdefault(str(n),[]).append([label,int(order),float(x),float(y),count])
  elif r.get('event')=='unit-stop-enter':count+=1
 return result

def validate_runtime(bundle,spec):
 if len(bundle['captures'])!=3:raise ValueError('two observations and observer-free control required')
 for i,c in enumerate(bundle['captures']):
  rows=c['rows'];meta=rows[0];footer=rows[-1];public=markers(c['preload'])
  if meta.get('mode')!=('control'if i==2 else'observe')or meta.get('sha256')!=SHA or not meta.get('owned')or meta['env']not in ('B','C')or meta['source_sha256']['map']!=spec['map_sha256']:
   raise ValueError('capture provenance differs')
  for p in SOURCES[:4]+SOURCES[-3:]:
   if meta['source_sha256'][Path(p).name]!=spec['source_sha256'][p]:raise ValueError('capture source differs '+p)
  if footer.get('event')!='preload-file'or not footer.get('complete')or footer['markers']!=110 or public!=spec['public']or hashlib.sha256(c['preload'].encode()).hexdigest()!=footer['sha256']:
   raise ValueError('public capture/control differs')
  if any(r.get('type')=='error'or r.get('event')=='trace-failed'for r in rows):raise ValueError('failed observer')
  native=[r for r in rows if 'seq'in r];ends=[r for r in rows if r.get('event')=='trace-end']
  if i==2:
   if native or ends or any(r.get('event')=='module'for r in rows):raise ValueError('control instrumented')
   continue
  if [r['seq']for r in native]!=list(range(1218))or len(ends)!=1 or not ends[0]['installed']or not ends[0]['readOnly']or ends[0]['depth']or ends[0]['counts']!=dict(Counter(r['event']for r in native)):
   raise ValueError('unbalanced/incomplete observer')
  if [r['value']for r in rows if r.get('event')=='marker']!=public or normalized(rows)!=spec['chronology']or projection(rows)!=spec['projection']:
   raise ValueError('frozen Stop chronology differs')
  for scene in range(16):
   events=[r for r in native if r['scene']==scene]
   entries=[r for r in events if r['event']=='unit-stop-enter']
   if len(entries)!=[2,2,3,4,3,4,4,4][scene%8]or any(r['caller']not in ('0x600433','0x5fc18a')for r in entries):raise ValueError('recovery ownership differs')
  # Each ordinary Stop's first cancellation recovery precedes notification,
  # and its new task follows the unit family. Callback terrain changes take
  # effect only in that second recovery, independently of previous movement.
  for scene in (0,1,8,9):
   p=spec['projection'][str(scene)]
   if [r[4]for r in p]!=[0,1,1,1,2]or p[-1][2:4]!=([624.0,592.0]if scene%8==1 else[656.0,656.0]):raise ValueError('Stop phase contract differs')
 if bundle['map_metadata']['map_sha256']!=spec['map_sha256']or bundle['map_metadata']['fine_origin']!=[0,0]or bundle['map_metadata']['shadow_size']!=4096:raise ValueError('map geometry differs')
 return dict(captures=2,controls=1,public_markers=330,native_events=2436,scenes=16)

def header(spec):
 s='/* Exported native-return/callback projection of retail Stop284.\n * Complete capture/control streams remain in retail-stop284-1.27.json.gz. */\n'
 s+='static struct {char const *label;uint32_t order;float x,y;unsigned recoveries;} const stop284_rows[16][9]={\n'
 for n in range(16):s+='    { '+','.join('{"%s",%u,%.3ff,%.3ff,%u}'%tuple(r)for r in spec['projection'][str(n)])+' },\n'
 return s+'};\n'

def validate(spec):
 if spec['version']!=1 or spec['task']!='MAP-04.2'or spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_patterns']!=TESTS:raise ValueError('Stop contract changed')
 for p,h in spec['source_sha256'].items():
  if digest(ROOT/p)!=h:raise ValueError('source changed '+p)
 if digest(BUNDLE)!=spec['bundle_sha256']:raise ValueError('bundle changed')
 bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()));validate_runtime(bundle,spec)
 if (ROOT/'games/warcraft-3/game/tests/fixtures/retail_stop284.h').read_text()!=header(spec):raise ValueError('engine projection differs from retail')
 return bundle

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());bundle=validate(spec);original_bytes(a.binary,spec['instructions'])
 import sys
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import stop284_oracle
 if stop284_oracle.original(a.binary)!=bundle['producer']:raise ValueError('original Stop producer differs')
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:
  runner.TESTS=TESTS;a.report.parent.mkdir(parents=True,exist_ok=True);engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 result=dict(passed=True,status='verified',**validate_runtime(bundle,spec),instructions=len(spec['instructions']),producer_operations=5,engine=engine,binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['scope'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
