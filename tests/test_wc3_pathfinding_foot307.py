"""Reject changed retail expectations and incomplete embedded-start controls."""
import gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_foot307 as v
class Foot307Evidence(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(v.SPEC.read_text());self.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  edit(self.bundle)
  with self.assertRaises(ValueError):v.validate_runtime(self.bundle,self.spec)
 def event(self,event,scene=0):return next(r for r in self.bundle['captures'][0]['rows']if r['event']==event and r.get('scene')==scene)
 def test_frozen_recovery_and_first_routes(self):self.assertEqual(v.validate(self.spec)['first_states'],54)
 def test_missing_control(self):self.reject(lambda b:b['captures'].pop())
 def test_hooked_control(self):self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='fine-enter')))
 def test_changed_velocity(self):self.reject(lambda b:self.event('commit-enter')['g']['members'][0]['m']['vel'].__setitem__(0,1))
 def test_changed_first_point(self):self.reject(lambda b:self.event('advance-leave',1)['point'].__setitem__(0,0))
 def test_changed_route_point(self):self.reject(lambda b:self.event('fine-leave',2)['points'][0].__setitem__(0,0))
 def test_changed_route_index(self):self.reject(lambda b:self.event('fine-leave',1).__setitem__('index',0))
 def test_missing_recovery(self):self.reject(lambda b:b['captures'][0]['rows'].remove(self.event('recover-leave')))
 def test_noncanonical_membership(self):self.reject(lambda b:self.event('commit-enter')['g']['members'][0]['m'].__setitem__('group',[0,0]))
 def test_changed_public_admission(self):self.reject(lambda b:b['captures'][2].__setitem__('preload',b['captures'][2]['preload'].replace('x=784.0000','x=800.0000')))
 def test_original_instruction_inventory(self):
  self.spec['instructions'].pop(next(iter(self.spec['instructions'])))
  with self.assertRaises(ValueError):v.validate(self.spec)
 def test_expected_words_cannot_follow_engine(self):
  self.spec['motion'][0]['rows'][1][5]^=1
  with self.assertRaises(ValueError):v.validate(self.spec)
if __name__=='__main__':unittest.main()
