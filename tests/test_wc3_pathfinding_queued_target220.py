"""Queued Move must resolve its target at activation and retain packet point words."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_queued_target220 as V

class QueuedTargetContractTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,fn):
  b=copy.deepcopy(self.bundle);fn(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def mutate(self,event,key,value):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('scene')==1 and r['event']==event).__setitem__(key,value))
 def test_original_repeats_and_unhooked_control(self):
  self.assertEqual(V.validate_runtime(self.bundle,V.validate(self.spec)),dict(captures=2,controls=1,scenes=6,admissions=12,fallbacks=10,public_markers=2739))
 def test_every_source_is_pinned(self):
  for p in V.SOURCES:
   s=copy.deepcopy(self.spec);s['pins'][p]='0'*64
   with self.assertRaises(ValueError):V.validate(s)
 def test_real_target_must_be_retained_at_shift_admission(self):
  def change(b):
   r=next(r for r in b['captures'][0]['rows']if r.get('scene')==1 and r['event']=='append-enter'and r['order']['flags']&4)
   r['order']['target']=V.INVALID
  self.reject(change)
 def test_admission_must_preserve_running_head(self):
  self.mutate('append-leave','state',dict(count=0))
 def test_fallback_must_use_packet_words(self):
  self.mutate('point-task','point',[0,0]);self.mutate('point-task','code',852339)
 def test_visible_control_must_keep_target_identity(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('scene')==6 and r['event']=='target-task').update(target=V.INVALID))
 def test_activation_must_follow_mutation(self):
  def change(b):
   r=next(r for r in b['captures'][0]['rows']if r.get('scene')==1 and r['event']=='dispatch-head'and r['order']['flags']&4)
   r['order']['identity']=V.INVALID
  self.reject(change)
 def test_repeats_and_control_are_required(self):
  for i in range(3):self.reject(lambda b:b['captures'].pop(i))
  self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))
 def test_public_movement_and_control_positions_are_required(self):
  for i in range(3):
   self.reject(lambda b:b['captures'][i].__setitem__('preload',b['captures'][i]['preload'].replace('label=settled','label=wrong')))
 def test_read_only_complete_owned_capture_is_required(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r['event']=='trace-end').update(readOnly=False))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
  self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
  self.reject(lambda b:b['captures'][0]['rows'].insert(1,dict(event='trace-failed')))
 def test_genuine_input_is_required(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r['event']=='player-input')['plan'].update(shift=False))
 def test_identity_numbers_may_vary_but_relationships_must_hold(self):
  b=copy.deepcopy(self.bundle);next(r for r in b['captures'][0]['rows']if r['event']=='module')['base']='0x10000000'
  self.assertEqual(V.validate_runtime(b,self.spec)['admissions'],12)
if __name__=='__main__':unittest.main()
