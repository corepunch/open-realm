#!/usr/bin/env python3
"""Verify ordered scripted ability contributions and nested modal admission."""
import argparse,gzip,hashlib,json,sys
from pathlib import Path
import verify_wc3_pathing_pause288 as parent
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
SPEC=ROOT/'tools/ghidra/fixtures/retail-pause289-1.27.json'
SCENES=[9,10,14]
SOURCES=['tools/ghidra/research/Pause289Evidence.java','tools/ghidra/research/pause289_oracle.py']
TESTS=['wc3_movement.pause289*','wc3_movement.pause288*','wc3_movement.pause287*','wc3_movement.pause286*',
 'wc3_save.*','wc3_order_reentry.*','wc3_order_lifecycle.*','wc3_ability_dispatch.*','wc3_spell.defend*',
 'wc3_movement.pathing_pause*','wc3_unit.unravenform_snaps_new_animation_frame_while_unit_is_paused',
 'wc3_unit.raven_morph_completion_and_takeoff_survive_move_order']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def projection(public):
 rows={str(n):[]for n in SCENES}
 for marker in public:
  a=dict(x.split('=',1)for x in marker.split()[1:]);n=int(a['scene'])
  if n not in SCENES or a['label']in('scene-start','settled','complete','start','selector-0'):continue
  rows[str(n)].append([a[k]for k in('label','order','paused','px','py','target','speed','defend')])
 return rows

def counter_projection(rows,classes):
 result=[];marker=None;index={n:-1 for n in SCENES};by_class={c['class']:c['vtable']for c in classes}
 for x in rows:
  if x['event']=='marker':
   marker=dict(i.split('=',1)for i in x['value'].split()[1:]);n=int(marker['scene'])
   if n in index and marker['label']not in('scene-start','settled','complete','selector-0','start'):index[n]+=1
  if x['event']!='state' or x.get('scene')not in index or not marker or marker['label']in('scene-start','settled','complete'):continue
  n=x['scene'];owners={z['vtable']:z['disabled'][:2]for z in x['state']['abilities']}
  result.append([n,index[n],marker['label'],owners[by_class['Adef']],owners[by_class['Amov']],x['state']['ability_disable']])
 return result

def header(spec):
 before=parent.SCENES
 try:parent.SCENES=SCENES;s=parent.header(spec['projection'],spec['counts']).replace('pause288','pause289')
 finally:parent.SCENES=before
 s+='\n/* Read-only ownership markers; original class getters identify Defend/Move. */\nstatic struct {unsigned scene,row;char const *label;int32_t defend[2],move[2],unit;} const pause289_counters[]={\n'
 return s+''.join('    {%u,%u,%s,{%d,%d},{%d,%d},%d},\n'%(n,i,json.dumps(label),*de,*mo,u)for n,i,label,de,mo,u in spec['counter_projection'])+'};\n'

def validate(spec):
 if spec['game_sha256']!=SHA or set(spec['source_sha256'])!=set(SOURCES) or spec['engine_tests']!=TESTS:raise ValueError('contract differs')
 for name,h in spec['source_sha256'].items():
  if digest(ROOT/name)!=h:raise ValueError('source changed '+name)
 if digest(parent.SPEC)!=spec['parent_sha256']:raise ValueError('retained packet parent changed')
 old=json.loads(parent.SPEC.read_text());base_bundle,base=parent.validate(old)
 bundle=json.loads(gzip.decompress(parent.SPEC.with_suffix('.json.gz').read_bytes()))
 parent.validate_ownership(old,bundle,base,base_bundle)
 if projection(base['public'])!=spec['projection']:raise ValueError('public projection changed')
 for capture in bundle['captures']:
  if counter_projection(capture['rows'],spec['classes'])!=spec['counter_projection'] or len(spec['counter_projection'])!=37:raise ValueError('per-owner counter boundary changed')
  counts={str(x['scene']):x['state']['count']for x in capture['rows']if x.get('event')=='pause-exit'and x.get('scene')in SCENES}
  if counts!=spec['counts']:raise ValueError('native total user count changed')
 if (ROOT/'games/warcraft-3/game/tests/fixtures/retail_pause289.h').read_text()!=header(spec):raise ValueError('engine fixture differs from retail')
 return dict(captures=2,control_in_parent=True,native_events=2086,counter_boundaries=74,public_scenes=len(SCENES))

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 spec=json.loads(SPEC.read_text());retail=validate(spec);original_bytes(a.binary,spec['instructions'])
 original_bytes(a.binary,json.loads(parent.SPEC.read_text())['instructions'])
 sys.path.insert(0,str(ROOT/'tools/ghidra/research'));import pause289_oracle
 if pause289_oracle.original(a.binary)!=spec['original'] or len(spec['original'])!=30 or pause289_oracle.classes(a.binary)!=spec['classes']:raise ValueError('original ordered walk/class getter differs')
 import verify_wc3_pathing_work242 as runner
 before=runner.TESTS
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=before
 report=dict(passed=True,status='verified',retail=retail,original_walks=30,original_classes=4,instructions=len(spec['instructions']),engine=engine,binary_sha256=SHA,game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=spec['scope'])
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
