"""Reject altered constructor positions, missing policies and damaged controls."""
import gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_place309 as v
class Place309Evidence(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(v.SPEC.read_text());self.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  edit(self.bundle)
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def event(self,name,scene=0):return next(x for x in self.bundle[0]['rows']if x['event']==name and x.get('scene')==scene)
 def test_frozen_contract(self):self.assertEqual(v.validate(self.spec)['states'],216)
 def test_missing_control(self):self.reject(lambda b:b.pop())
 def test_hooked_control(self):self.reject(lambda b:b[2]['rows'].insert(1,dict(event='trace-end')))
 def test_changed_fine_pose(self):self.reject(lambda b:self.event('delta-leave')['m']['pos'].__setitem__(0,1))
 def test_changed_clamp(self):self.reject(lambda b:self.event('clamp-leave')['point'].__setitem__(0,1))
 def test_changed_result(self):self.reject(lambda b:self.event('admit-leave').__setitem__('result',0))
 def test_flyer_policy(self):self.reject(lambda b:self.event('placement-enter',30).__setitem__('integer',0))
 def test_changed_mask(self):self.reject(lambda b:self.event('placement-enter',60).__setitem__('mask',2))
 def test_changed_radius(self):self.reject(lambda b:self.event('placement-enter',18).__setitem__('radius',0))
 def test_lost_record(self):self.reject(lambda b:b[0]['rows'].remove(self.event('delta-enter')))
 def test_expected_pose_cannot_follow_engine(self):
  self.spec['states'][0][0]['fine'][0]^=1
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_missing_instruction_guard(self):
  self.spec['instructions'].pop(next(iter(self.spec['instructions'])))
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
if __name__=='__main__':unittest.main()
