"""Reject altered retail goal words, placement policy and capture provenance."""
import gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_goal308 as v
class Goal308Evidence(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(v.SPEC.read_text());self.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  edit(self.bundle)
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def event(self,event,scene=0,part='goal308'):
  return next(r for r in self.bundle[part][0]['rows']if r['event']==event and r.get('scene')==scene)
 def test_frozen_contract(self):
  result=v.validate(self.spec);self.assertEqual(result['motion_states'],1945);self.assertEqual(result['first_route_points'],300)
 def test_missing_unhooked_control(self):self.reject(lambda b:b['goal308'].pop())
 def test_hooked_control(self):self.reject(lambda b:b['place308'][2]['rows'].insert(1,dict(event='trace-end')))
 def test_changed_velocity(self):self.reject(lambda b:self.event('commit-leave')['g']['members'][0]['m']['vel'].__setitem__(0,1))
 def test_changed_route(self):self.reject(lambda b:self.event('fine-leave',5)['points'][0].__setitem__(0,0))
 def test_changed_return(self):self.reject(lambda b:self.event('advance-leave',6).__setitem__('result',self.event('advance-leave',6)['result']^1))
 def test_lost_event(self):self.reject(lambda b:b['goal308'][0]['rows'].remove(self.event('commit-enter')))
 def test_changed_clamp(self):self.reject(lambda b:self.event('clamp-leave',2,'place308')['point'].__setitem__(0,0))
 def test_changed_placement(self):self.reject(lambda b:self.event('position-leave',3,'place308')['point'].__setitem__(0,0))
 def test_expected_words_cannot_follow_engine(self):
  self.spec['motion'][8]['rows'][34][9]^=1
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_missing_instruction_guard(self):
  self.spec['instructions'].pop(next(iter(self.spec['instructions'])))
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
if __name__=='__main__':unittest.main()
