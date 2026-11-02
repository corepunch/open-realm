#!/usr/bin/env python3
"""Rebuild archived subscriber evidence; reject incomplete or altered captures.

Original instruction execution is a separate fresh oracle entry. This verifier
never treats the archived oracle report used by the expected builder as fresh.
"""
import argparse,json,re,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from verify_attack173_orders import module,digest,BINARY_SHA
EXPECTED=Path('tools/ghidra/fixtures/research/ORDER-03.1-expected.json')
SHA='595cd7ed40c60ac8a0eb9a203bb471152a36acf3ec9d587ac0587c1af60c7ed9'


def claims(data):
 failures=[]
 expected={
  'forward':{1:['P1','T1','T2','T3','T4'],2:['P1','T1','T2','T4'],3:['P1','T4','T5'],10:['P1','G1','G2'],11:['P1','G1','G2']},
  'reverse':{1:['P1','T4','T3','T2','T1'],2:['P1','T4','T3','T2'],3:['P1','T4','T3'],10:['P1','G1','G2'],11:['P1','G1','G2']}}
 for variant,phases in expected.items():
  markers=data['live']['controls'][variant]
  for phase,names in phases.items():
   rows=[s for s in markers if re.search(r'phase='+str(phase)+r'\b',s)]
   entered=[re.search(r'enter trig=(\S+)',s)[1]for s in rows if 'enter trig='in s]
   if entered!=names:failures.append((variant,phase,'delivery'))
   starts=[i for i,s in enumerate(rows)if 'issue begin 'in s]
   ends=[i for i,s in enumerate(rows)if 'issue end 'in s]
   actions=[i for i,s in enumerate(rows)if 'enter trig='in s or 'exit trig='in s]
   if len(starts)!=1 or len(ends)!=1 or not actions or not all(starts[0]<i<ends[0]for i in actions):
    failures.append((variant,phase,'synchronous'))
  self_exit=[s for s in markers if 'phase=2 'in s and 'exit trig=T2 'in s]
  if len(self_exit)!=1 or not self_exit[0].endswith('eval=0 exec=0'):failures.append((variant,'destroyed-counts'))
 if not all(data['live']['observer_equals_control'].values()):failures.append('control')
 repeat=data['live']['repeat']['forward-observe-1 vs 2 (CUnit destructor rows excluded)']
 if repeat is not True:failures.append('repeat')
 return failures


def check_capture(raw,sha,mode):
 assert digest(raw)==sha,'capture hash differs'
 rows=[json.loads(line)for line in raw.splitlines()]
 metas=[r for r in rows if r.get('event')=='metadata']
 ends=[r for r in rows if r.get('event')==('trace-end'if mode=='observe'else'control-end')]
 assert len(metas)==len(ends)==1 and metas[0]['sha256']==BINARY_SHA
 assert metas[0]['mode']==mode
 if mode=='observe':assert ends[0]['installed'] is True
 assert not any(r.get('event')in('error','script-error','exception')for r in rows)
 assert any(r.get('event')=='artifacts'for r in rows)
 return len(rows)


def verify(archive):
 raw=EXPECTED.read_bytes();assert digest(raw)==SHA;frozen=json.loads(raw)
 builder=module('order177_expected',Path('tools/frida/research/order03_expected.py'))
 current=builder.build(archive,'ORDER-03.1');assert current==frozen,'reconstructed evidence differs'
 assert current['binary_sha256']==BINARY_SHA and not claims(current)
 captures=records=0
 for name,sha in frozen['live']['captures'].items():
  if not name.endswith('.jsonl'):continue
  records+=check_capture((archive/'ORDER-03.1/captures'/name).read_bytes(),sha,'observe'if'-observe-'in name else'control')
  captures+=1
 return dict(passed=True,status='retail-order-subscriber-dispatch',binary_sha256=BINARY_SHA,
  captures=captures,records=records,observer_free_comparisons=3,matched_repeats=1,
  public_claims=10,scope='Synchronous player-before-unit delivery, insertion cutoff, immediate destroy suppression and next primary-clock cleanup.',
  exclusions=['Archived read-only Frida captures; no new live run.','Physical CUnit destructor timing excluded from normalized repeat.','Fresh original instruction execution is a separate oracle, not the archived report.','Nested death/Stop/removal and payload lifetime remain ORDER-03.2.'])


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 assert not a.output.exists();report=verify(a.archive)
 a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
