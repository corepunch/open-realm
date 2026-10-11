#!/usr/bin/env python3
"""Verify actual Captain actor relocation with retained velocity and request ownership."""
import argparse,copy,gzip,hashlib,json,re,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-teleport290-1.27.json'
SOURCES=['tools/frida/research/teleport290_'+s for s in ['make_map.py','probe.j','probe.ai','observer.js']]+['tools/frida/research/captain_enrollment157_capture.py','tools/frida/research/GROUP-03.4.7.3_make_map.py','tools/frida/make_wc3_pathfinding_map.py','tools/ghidra/research/teleport290_oracle.py','tools/ghidra/research/Teleport290Evidence.java']
TESTS=['wc3_movement.teleport290*','wc3_bot.*','wc3_movement.public_captain*','wc3_movement.capscope273*','wc3_save.*']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def public(text):return re.findall(r'call Preload\( "(R290[^"\r\n]*)" \)',text)
def normalized(rows):
 result=[]
 for x in rows:
  if x.get('event')not in('teleport-enter','teleport-leave'):continue
  x=copy.deepcopy(x);x.pop('ms',None);s=x.get('before',x.get('after'));s.pop('captain');s['actor'].pop('mover')
  #58/60 are scalar values.5c/64 here are unsourced FloatMini auxiliary words,
  # not additional numeric coordinates. Raw captures preserve them separately.
  s['current']=[s['current'][0],s['current'][2]];result.append(x)
 return result

def header(native):
 s='/* Unchanged word observations from two read-only TeleportCaptain repeats. */\nstatic struct {uint32_t request[2],before[2],velocity[2],after[2],members;} const teleport290_cases[]={\n'
 for a,b in zip(native[::2],native[1::2]):
  v=a['xy']+a['before']['actor']['position']+a['before']['actor']['velocity']+b['after']['actor']['position']
  s+='    {{%s},{%s},{%s},{%s},%u},\n'%tuple([','.join('0x%08xu'%z for z in v[i:i+2])for i in range(0,8,2)]+[a['before']['counts'][1]])
 return s+'};\n'

def validate(spec):
 if spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES)or spec['engine_tests']!=TESTS:raise ValueError('source/producer contract differs')
 for name,h in spec['source_sha256'].items():
  if digest(ROOT/name)!=h:raise ValueError('source changed '+name)
 path=SPEC.with_suffix('.json.gz')
 if digest(path)!=spec['bundle_sha256']:raise ValueError('capture bundle changed')
 bundle=json.loads(gzip.decompress(path.read_bytes()))
 if len(bundle['captures'])!=2 or len(bundle['controls'])!=1:raise ValueError('missing repeat/control')
 for c in bundle['captures']+bundle['controls']:
  rows=c['rows'];meta=rows[0]
  if meta.get('event')!='metadata'or not meta.get('owned')or meta.get('sha256')!=SHA or meta['source_sha256']['map']!=spec['map_sha256']:raise ValueError('capture ownership/build/map')
  if meta['source_sha256']['teleport290_observer.js']!=spec['source_sha256']['tools/frida/research/teleport290_observer.js']or meta['source_sha256']['captain_enrollment157_capture.py']!=spec['source_sha256']['tools/frida/research/captain_enrollment157_capture.py']:raise ValueError('observer/controller changed')
  if any(x.get('type')=='error'for x in rows)or not any(x.get('event')=='preload-file'and x.get('complete')for x in rows):raise ValueError('incomplete/failed capture')
  if public(c['preload'])!=spec['public']or len(spec['public'])!=250:raise ValueError('public actor-query/member trajectory changed')
  if c in bundle['captures']:
   if meta['mode']!='observe'or not any(x.get('event')=='trace-end'and x.get('installed')for x in rows):raise ValueError('uninstalled observer')
   if normalized(rows)!=spec['native']or len(spec['native'])!=14:raise ValueError('native relocation boundary changed')
  elif meta['mode']!='control'or any(x.get('event')in('module','teleport-enter','teleport-leave','snapshot')for x in rows):raise ValueError('control instrumented')
 for a,b in zip(spec['native'][::2],spec['native'][1::2]):
  if a['event']!='teleport-enter'or b['event']!='teleport-leave'or a['xy']!=b['xy']or b['recovery_calls']!=0:raise ValueError('relocation called Stop recovery')
  before=copy.deepcopy(a['before']);after=copy.deepcopy(b['after']);before['actor'].pop('position');after['actor'].pop('position')
  if before!=after:raise ValueError('relocation lost velocity/request/membership')
 if (ROOT/'games/warcraft-3/game/tests/fixtures/retail_teleport290.h').read_text()!=header(spec['native']):raise ValueError('engine expectation differs from retail')
 return dict(captures=2,controls=1,public_markers=250,native_boundaries=28,relocations=7)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());retail=validate(spec);original_bytes(a.binary,spec['instructions'])
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import teleport290_oracle
 if teleport290_oracle.original(a.binary)!=spec['original']:raise ValueError('original wrapper differs')
 import verify_wc3_pathing_work242 as runner
 old=runner.TESTS
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report,timeout=300)
 finally:runner.TESTS=old
 report=dict(passed=True,status='verified',retail=retail,original_wrappers=len(spec['original']),instructions=len(spec['instructions']),engine=engine,binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
