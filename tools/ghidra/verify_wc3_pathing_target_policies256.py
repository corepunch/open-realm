#!/usr/bin/env python3
"""Join all frozen target-loss policies to actual engine owner regressions."""
import argparse,hashlib,json,re,sys
from pathlib import Path
from verify_wc3_pathing_target_normalize218 import original_bytes,SHA
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
from verify_target166_live import verify as verify_visibility
from verify_target167_live import scene_rows
FIXTURE=ROOT/'tools/ghidra/fixtures/retail-target-policies256-1.27.json'
VISIBILITY='tools/ghidra/fixtures/retail-target-visibility166-1.27.json'
LOSS='tools/ghidra/fixtures/research/TARGET-03.2-expected.json'
REACQUIRE='tools/ghidra/fixtures/research/TARGET-03.2-expected-reacquire.json'
HEADER='games/warcraft-3/game/tests/retail_target_fog166.h'
SOURCES=[VISIBILITY,LOSS,REACQUIRE,HEADER,'tools/ghidra/research/Work256Evidence.java',
 'tools/ghidra/research/verify_target166_live.py','tools/ghidra/research/verify_target167_live.py']
TESTS=['wc3_movement.target166*','wc3_movement.target167*','wc3_movement.target168*',
 'wc3_movement.target182*','wc3_movement.target216*','wc3_movement.target221*',
 'wc3_movement.target222*','wc3_movement.target223*','wc3_movement.target224*',
 'wc3_movement.target251*','wc3_movement.target252*','wc3_movement.target254*','wc3_movement.target256*',
 'wc3_movement.public_smart_follow_matches_original_target_remove_reuse',
 'wc3_movement.public_smart_follow_matches_original_target_kill_reuse',
 'wc3_movement.pathing_pause_and_displacement_match_original_and_saved_continuations',
 'wc3_order_subscribers.*','wc3_save.rejects_prior_save_versions']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def pause_resume(rows):
 rows=scene_rows(rows,12)
 def marker(text):
  found=[(i,r)for i,r in enumerate(rows)if r.get('event')=='marker'and text in r['value']]
  if len(found)!=1:raise ValueError('pause marker missing')
  return found[0]
 paused,paused_marker=marker('l=60 label=end-produce');begin,a=marker('l=120 label=begin-undo');end,b=marker('l=120 label=end-undo')
 if a['c']!=b['c']or ',851973'not in b['value']or ',851971'not in b['value']:raise ValueError('unpause head boundary changed')
 followers=[r for r in rows[paused:begin]if r.get('event')=='gtick'and r['target']!=[0xffffffff]*2]
 if not followers:raise ValueError('retained follower missing')
 target=followers[0]['target']
 if any(r.get('event')=='target-lost'for r in rows[paused:end]):raise ValueError('pause emitted target loss')
 retired=[r for r in rows[paused:begin]if r.get('event')=='gtick'and any(m.get('id')==target for m in r['members'])]
 if len(retired)!=1 or retired[0]['c']!=paused_marker['c']+1 or any(m['vel']!=[0,0]for m in retired[0]['members']):raise ValueError('paused target retirement changed')
 resumed=[r for r in rows[end+1:]if r.get('event')=='gtick'and any(m.get('id')==target for m in r['members'])][:2]
 if len(resumed)!=2:raise ValueError('resume physical owner missing')
 first=resumed[0]
 if first['c']!=a['c']+1 or first['age']!=0 or first['flags']!=0x10000 or first['target']!=[0xffffffff]*2:raise ValueError('resume owner publication changed')
 visits=[r for r in rows[end+1:]if r.get('event')=='gtick'and r['c']==first['c']]
 if visits[0]!=first:raise ValueError('newest resumed owner no longer precedes follower')
 result=[]
 for r in resumed:
  m=next(m for m in r['members']if m.get('id')==target)
  result.append([r['c'],r['flags'],r['age'],r['cd'],*m['pos'],*m['vel'],*r['path']['cnt'],*r['path']['idx']])
 return result

def validate(s):
 if s['version']!=1 or s['task']!='TARGET-03.2'or s['game_sha256']!=SHA or s['engine_tests']!=TESTS or set(s['pins'])!=set(SOURCES):raise ValueError('policy contract changed')
 for p,h in s['pins'].items():
  if digest(ROOT/p)!=h:raise ValueError('source changed '+p)
 for family,path,count in(('loss',LOSS,17),('reacquire',REACQUIRE,5)):
  original=json.loads((ROOT/path).read_text())['scenes']
  entries=[r for r in s['coverage']if r['family']==family]
  if [r['scene']for r in entries]!=list(range(count)):raise ValueError('policy coverage missing or duplicate')
  for r,old in zip(entries,original):
   if r['name']!=old['name']or r['outcome']!=old['outcome']or not r['engine']or any(x not in TESTS for x in r['engine']):raise ValueError('policy evidence or engine coverage changed')
 if len(s['coverage'])!=22:raise ValueError('unassigned policy')
 if len(s['pause_rows'])!=2 or any(pause_resume(r)!=s['pause_resume']for r in s['pause_rows']):raise ValueError('frozen resume witness changed')
 return s

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True)
 p.add_argument('--archive',type=Path,default=Path('/GitHub/wc3-analysis/reports/pathfinding-1.27/research'))
 p.add_argument('--test-binary',type=Path,default=ROOT/'build/bin/openwarcraft3-tests');p.add_argument('--data',type=Path,default=ROOT/'build/tests');p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 s=validate(json.loads(FIXTURE.read_text()));original_bytes(a.binary,dict(s['instructions']))
 frozen=json.loads((ROOT/VISIBILITY).read_text());live=verify_visibility(frozen,a.archive,ROOT/HEADER)
 witnesses=[]
 for name,pin in frozen['captures'].items():
  if pin['family']=='loss'and pin['metadata']['mode']=='observe':
   actual=pause_resume([json.loads(line)for line in(a.archive/name).read_text().splitlines()])
   if actual!=s['pause_resume']:raise ValueError('original resumed point owner changed')
   witnesses.append(actual)
 if len(witnesses)!=2:raise ValueError('resume repeats missing')
 import verify_wc3_pathing_work242 as runner
 old=runner.TESTS;a.report.parent.mkdir(parents=True,exist_ok=True)
 try:runner.TESTS=TESTS;engine=runner.run_engine(a.test_binary,a.data,a.report)
 finally:runner.TESTS=old
 result=dict(passed=True,task=s['task'],retail=live,policies=len(s['coverage']),resume_repeats=2,
  instructions=len(s['instructions']),engine=engine,binary_sha256=SHA,fixture_sha256=digest(FIXTURE),
  game_library_sha256=digest(a.test_binary.parent.parent/'lib/libgame-wc3-test.so'),limits=s['limits'])
 a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
