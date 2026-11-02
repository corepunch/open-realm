#!/usr/bin/env python3
"""Verify point/flying spell ownership against complete original268 repeats/control."""
import argparse,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-spell268-1.27.json'
SOURCES=['tools/ghidra/research/Spell268Evidence.java','tools/frida/research/owner268_probe.j',
 'tools/frida/research/owner268_make_map.py','tools/frida/research/owner268_observer.js',
 'tools/frida/research/spell184_capture.py','tools/frida/research/target021_make_map.py']
TESTS=['wc3_spell.owner268*','wc3_spell.owner267*','wc3_spell.graph266*',
 'wc3_spell.approach185*','wc3_spell.range184*','wc3_spell.unit_target_approach*',
 'wc3_order_reentry.*','wc3_order_lifecycle.*','wc3_interrupt.*','wc3_save.rejects_prior_save_versions']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def heads(markers):
 out=[]
 for v in markers:
  f=dict(p.split('=',1)for p in v.split()[1:])
  if 'order'in f:out.append([int(f['s']),int(f['l']),f['label'],int(f['order'])])
 return out
def expected_heads():
 out=[]
 for scene in range(4):
  command=852488 if scene<2 else 852092
  out.extend([[scene,0,'created',0],[scene,0,'old-move',851986],[scene,0,'accepted',command]])
  for tick in range(1,121):
   if scene%2 and tick==10:out.append([scene,tick,'stopped',0])
   cut=10 if scene%2 else 32 if scene==0 else 21
   out.append([scene,tick,'sample',command if tick<cut else 0])
 return out
def validate(s):
 if(s['version']!=1 or s['task']!='ORDER-01.13' or s['game_sha256']!=SHA or set(s['pins'])!=set(SOURCES)or
    s['engine_tests']!=TESTS or len(s['instructions'])!=s['instruction_count']or
    len(s['markers'])!=507 or heads(s['markers'])!=expected_heads()):raise ValueError('spell268 contract differs')
 for p,h in s['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('spell268 evidence changed '+p)
 return s

def verify_live(root,s):
 if [p['mode']for p in s['captures']]!=['observe','observe','control']:raise ValueError('missing repeats/control')
 observed=[]
 for pin in s['captures']:
  p=root/pin['file'];pre=p.with_name(p.stem+'-preload.txt')
  if digest(p)!=pin['sha256']or digest(pre)!=pin['preload_sha256']:raise ValueError('capture changed')
  rows=[json.loads(l)for l in p.read_text().splitlines()];meta=rows[0];footer=rows[-1]
  if(meta.get('sha256')!=SHA or not meta.get('owned')or meta.get('mode')!=pin['mode']or
     meta['display']not in(':98',':99')or meta['remote']not in('127.0.0.1:27049','127.0.0.1:27050')or
     meta['source_sha256']['map']!=s['map_sha256']or meta['source_sha256']['owner268_observer.js']!=s['pins'][SOURCES[3]]or
     meta['source_sha256']['spell184_capture.py']!=s['pins'][SOURCES[4]]or
     footer.get('event')!='preload-file'or not footer.get('complete')or footer['markers']!=507 or
     footer['sha256']!=pin['preload_sha256']or any(r.get('event')in('trace-failed','error')or r.get('type')=='error'for r in rows)):raise ValueError('incomplete/unowned capture')
  markers=re.findall(r'call Preload\( "(O268 [^"\r\n]*)" \)',pre.read_text())
  if markers!=s['markers']:raise ValueError('public repeat/control differs')
  if pin['mode']=='control':
   if any(r.get('event')in('module','trace-end','marker','current-query','ability-order')for r in rows):raise ValueError('hooked control')
   continue
  end=next((r for r in rows if r.get('event')=='trace-end'),{})
  if not end.get('installed')or end.get('active')or [r['value']for r in rows if r.get('event')=='marker']!=markers:raise ValueError('incomplete observer')
  queries=[[r['scene'],r['command']]for r in rows if r.get('event')=='current-query']
  if queries!=[[r[0],r[3]]for r in expected_heads()]:raise ValueError('native getter differs')
  orders=[[r['scene'],r['command'],r['vtable'],r['result']]for r in rows if r.get('event')=='ability-order-result']
  if orders!=s['orders']:raise ValueError('owning ability dispatch differs')
  if any(r.get('event')=='target-producer'for r in rows):raise ValueError('unexpected continuation witness')
  observed.append(queries)
 return dict(captures=3,observed_repeats=2,controls=1,public_markers=1521,public_head_queries=1482,native_queries=988,ability_dispatches=24)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--archive',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=validate(json.loads(FIXTURE.read_text()));original_bytes(a.binary,spec['instructions']);live=verify_live(a.archive,spec)
 import verify_wc3_pathing_work242 as engine
 before=engine.TESTS
 try:engine.TESTS=TESTS;checks=engine.run_engine(a.test_binary,a.data,a.report)
 finally:engine.TESTS=before
 result=dict(passed=True,task=spec['task'],**live,instructions=len(spec['instructions']),engine=checks,
 binary_sha256=SHA,fixture_sha256=digest(FIXTURE),test_binary_sha256=digest(a.test_binary),
 game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
