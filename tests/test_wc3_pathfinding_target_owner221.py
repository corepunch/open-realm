"""Owner change is an independent subscription with task-specific recovery."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_target_owner221 as V

class TargetOwnerContractTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,fn):
  b=copy.deepcopy(self.bundle);fn(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def change(self,scene,event,fn):
  self.reject(lambda b:fn(next(r for r in b['captures'][0]['rows']if r.get('scene')==scene and r['event']==event and r.get('tick',0)%70==20)))
 def test_original_repeats_and_unhooked_control(self):
  self.assertEqual(V.validate_runtime(self.bundle,V.validate(self.spec)),dict(captures=2,controls=1,scenes=7,deliveries=10,public_markers=1488))
 def test_sources_and_bundle_are_pinned(self):
  for p in V.SOURCES:
   s=copy.deepcopy(self.spec);s['pins'][p]='0'*64
   with self.assertRaises(ValueError):V.validate(s)
  s=copy.deepcopy(self.spec);s['bundle_sha256']='0'*64
  with self.assertRaises(ValueError):V.validate(s)
 def test_event_must_reach_the_actual_target_subscriber(self):
  self.change(0,'owner-handler-enter',lambda r:r.update(target=V.INVALID))
  self.change(0,'owner-handler-enter',lambda r:r['unit'].update(identity=V.INVALID))
 def test_persistent_follow_must_end(self):
  self.change(0,'owner-handler-leave',lambda r:r.update(target=[1,1]))
  self.change(0,'owner-handler-leave',lambda r:r['unit'].update(head=[1,1]))
 def test_approach_must_retain_head_and_advance_task(self):
  self.change(4,'owner-handler-leave',lambda r:r.update(target=V.INVALID))
  self.change(4,'owner-handler-leave',lambda r:r['unit'].update(task=V.INVALID))
  self.change(4,'owner-handler-leave',lambda r:r['unit'].update(head=V.INVALID))
 def test_target_clear_precedes_recovery_and_arrival(self):
  self.change(0,'recover',lambda r:r.update(caller='0'))
  self.change(0,'arrival',lambda r:r.update(event='clear-target'))
 def test_owner_change_is_not_target_lost(self):
  def change(b):
   rows=b['captures'][0]['rows'];r=next(r for r in rows if r.get('scene')==0 and r['event']=='recover');r['event']='target-lost'
  self.reject(change)
 def test_same_owner_does_not_deliver_to_follow(self):
  self.change(2,'owner-leave',lambda r:r.update(event='owner-handler-leave'))
 def test_public_output_is_compared_in_both_repeats_and_control(self):
  for i in range(3):
   self.reject(lambda b:b['captures'][i].__setitem__('preload',b['captures'][i]['preload'].replace('label=settled','label=wrong')))
 def test_complete_owned_readonly_capture_is_required(self):
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
  self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r['event']=='trace-end').update(readOnly=False))
 def test_repeats_and_unhooked_control_are_required(self):
  for i in range(3):self.reject(lambda b:b['captures'].pop(i))
  self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))
 def test_runtime_addresses_are_not_fixed_expectations(self):
  b=copy.deepcopy(self.bundle);next(r for r in b['captures'][0]['rows']if r['event']=='module')['base']='0x10000000'
  self.assertEqual(V.validate_runtime(b,self.spec)['deliveries'],10)
if __name__=='__main__':unittest.main()
