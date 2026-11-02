"""Reject changes to captured goal motion, target completion and controls."""
import gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_goal310 as v
class Goal310Evidence(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(v.SPEC.read_text());self.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  edit(self.bundle)
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def event(self,name,scene=0):return next(x for x in self.bundle[0]['rows']if x['event']==name and x.get('scene')==scene)
 def test_frozen_contract(self):self.assertEqual(v.validate(self.spec)['perimeter_completions'],9)
 def test_missing_control(self):self.reject(lambda b:b.pop())
 def test_hooked_control(self):self.reject(lambda b:b[2]['rows'].insert(1,dict(event='trace-end')))
 def test_changed_fine_pose(self):self.reject(lambda b:self.event('commit-leave')['g']['members'][0]['m']['pos'].__setitem__(0,1))
 def test_changed_velocity(self):self.reject(lambda b:self.event('commit-enter',38)['g']['members'][0]['m']['vel'].__setitem__(0,1))
 def test_changed_route(self):self.reject(lambda b:self.event('fine-leave',70)['points'][0].__setitem__(0,1))
 def test_lost_record(self):self.reject(lambda b:b[0]['rows'].remove(self.event('advance-enter')))
 def test_changed_perimeter_result(self):
  self.reject(lambda b:next(x for x in b[0]['rows']if x['event']=='advance-leave'and x['result']==3).__setitem__('result',4))
 def test_changed_removal_frontier(self):
  self.spec['motion'][3]['removal_index']+=1
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_expected_order_cannot_follow_engine(self):
  self.spec['motion'][6]['final_order']=851986
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_expected_pose_cannot_follow_engine(self):
  self.spec['motion'][0]['rows'][0][9]^=1
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_missing_instruction_guard(self):
  self.spec['instructions'].pop(next(iter(self.spec['instructions'])))
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
if __name__=='__main__':unittest.main()
