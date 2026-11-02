#!/usr/bin/env python3
"""Verify original point-spell producers and frozen physical approach words."""
import argparse,hashlib,json,re
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-spell269-1.27.json'
HEADER='games/warcraft-3/game/tests/fixtures/retail_spell_approach269.h'
SOURCES=['tools/ghidra/research/Spell269Evidence.java','tools/frida/research/spell269_observer.js',
 'tools/frida/research/owner268_probe.j','tools/frida/research/owner268_make_map.py',
 'tools/frida/research/spell184_capture.py','tools/frida/research/target021_observer.js',
 'tools/ghidra/fixtures/retail-spell268-1.27.json',HEADER]
TESTS=['wc3_movement.spell269*','wc3_spell.point269*','wc3_movement.spell185*',
 'wc3_spell.owner268*','wc3_spell.graph266*','wc3_spell.approach185*',
 'wc3_spell.unit_target_approach*','wc3_spell.point_spell_order_approach_round_trips_save',
 'wc3_spell.unit_or_point_approach_keeps_its_point_target_shape','wc3_items.point_target_item_walks_into_range_then_places_at_clicked_point',
 'wc3_order_reentry.*','wc3_order_lifecycle.*','wc3_interrupt.*',
 'wc3_save.rejects_invalid_physical_group_payloads','wc3_save.rejects_prior_save_versions']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def header(motion):
 return ('/* Original retail Flame Strike point approach; Payoff269.\n'
 ' * counter, primary clock, committed fine position/velocity, captured range. */\n'
 'static uint32_t const spell269_motion[][9]={\n'+
 ''.join('    {'+','.join('0x%08xu'%v for v in row)+'},\n'for row in motion)+'};\n')
def validate(s):
 if(s['version']!=1 or s['task']!='ORDER-01.13' or s['game_sha256']!=SHA or set(s['pins'])!=set(SOURCES)or
    s['engine_tests']!=TESTS or len(s['instructions'])!=s['instruction_count']or len(s['motion'])!=52 or
    any(len(r)!=9 or r[0]!=1091+i or r[8]!=0x41c80000 for i,r in enumerate(s['motion']))or
    header(s['motion'])!=(ROOT/HEADER).read_text()):raise ValueError('point approach contract differs')
 for p,h in s['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('changed point evidence '+p)
 return s
IDENTITIES={'p','m','id','grp','ft','links'}
def state(v):
 if isinstance(v,dict):return {k:state(x)for k,x in v.items()if k not in IDENTITIES}
 if isinstance(v,list):return [state(x)for x in v]
 return v

def extract(rows):
 clocks={(r['c'],r.get('g')):r['clock']for r in rows if r['event']=='spell-owner-clock'and r['phase']=='begin'}
 scene=-1;source=None;motion=[];tasks=[];ranges=[];producers=[];visits=[]
 for r in rows:
  e=r['event']
  if e=='marker'and'label=begin-setup'in r['value']:scene=int(re.search(r' s=(-?\d+)',r['value'])[1])
  if e=='begin-point':
   tasks.append([scene,r['c'],*r['point'],r['range'],r['arrival'],r['cantpath'],r['flag']])
   if scene==0 and r['range']==0x44480000:source=r['self']
  if e=='spell-point-range':ranges.append([scene,r['range'],r['buffer'],*r['point']])
  if e=='spell-initial':producers.append([scene,r['target']!='0x0',r['widget']!='0x0',*r['point']])
  if e!='gtick':continue
  visits.append([scene,r['c'],r['flags'],r['age'],r['comp'],r['cd'],r['unseen'],state(r['path']),state(r['members'])])
  if scene==0:
   # A retired owner still retains a stale row pointing at the same mover.
   # Its current group identity must match this owner before it is replayed.
   m=next((m for m in r['members']if m.get('id')==source and m['grp']==r['id']),None)
   if m:motion.append([r['c'],*clocks[r['c'],r['g']],*m['pos'],*m['vel'],m['range']])
 return dict(motion=motion,tasks=tasks,ranges=ranges,producers=producers,visits=visits)

def verify_live(root,s):
 if [p['mode']for p in s['captures']]!=['observe','observe','control']:raise ValueError('missing repeat/control')
 markers=json.loads((ROOT/SOURCES[6]).read_text())['markers'];observed=[]
 for pin in s['captures']:
  p=root/pin['file'];pre=p.with_name(p.stem+'-preload.txt')
  if digest(p)!=pin['sha256']or digest(pre)!=pin['preload_sha256']:raise ValueError('capture changed')
  rows=[json.loads(l)for l in p.read_text().splitlines()];meta=rows[0];last=rows[-1]
  if(meta.get('sha256')!=SHA or not meta.get('owned')or meta.get('mode')!=pin['mode']or
     meta['display']not in(':98',':99')or meta['remote']not in('127.0.0.1:27049','127.0.0.1:27050')or
     meta['source_sha256']['map']!=s['map_sha256']or
     last.get('event')!='preload-file'or not last.get('complete')or last['markers']!=507 or
     last['sha256']!=pin['preload_sha256']or any(r.get('event')in('trace-failed','error')or r.get('type')=='error'for r in rows)):
   raise ValueError('incomplete/unowned point capture')
  for path in (SOURCES[1],SOURCES[4],SOURCES[5]):
   if meta['source_sha256'][Path(path).name]!=s['pins'][path]:raise ValueError('captured source differs')
  if re.findall(r'call Preload\( "(O268 [^"\r\n]*)" \)',pre.read_text())!=markers:raise ValueError('old public expectations changed')
  if pin['mode']=='control':
   if any(r['event']in('module','trace-end','marker','gtick','spell-initial')for r in rows):raise ValueError('hooked control')
   continue
  ends=[r for r in rows if r['event']=='trace-end']
  if(len(ends)!=1 or not ends[0].get('installed')or
     any(k.endswith('-dropped')and v for k,v in ends[0]['counts'].items())or
     [r['value']for r in rows if r['event']=='marker']!=markers):raise ValueError('incomplete observer')
  out=extract(rows)
  if len(out['visits'])!=169 or any(out[k]!=s[k]for k in('motion','tasks','ranges','producers')):raise ValueError('physical producer/motion differs')
  observed.append(out['visits'])
 if observed[0]!=observed[1]:raise ValueError('complete ordered owner repeat differs')
 return dict(captures=3,observed_repeats=2,controls=1,public_markers=1521,raw_owner_visits=338,motion_rows=52,point_bridge_calls=12,initial_spell_calls=8)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--archive',type=Path,required=True)
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 s=validate(json.loads(FIXTURE.read_text()));original_bytes(a.binary,s['instructions']);live=verify_live(a.archive,s)
 import verify_wc3_pathing_work242 as engine
 before=engine.TESTS
 try:engine.TESTS=TESTS;checks=engine.run_engine(a.test_binary,a.data,a.report)
 finally:engine.TESTS=before
 result=dict(passed=True,task=s['task'],**live,instructions=len(s['instructions']),engine=checks,
 binary_sha256=SHA,fixture_sha256=digest(FIXTURE),test_binary_sha256=digest(a.test_binary),
 game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
