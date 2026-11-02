"""The retail word fixtures must not be changed to follow engine output."""
import gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_numeric312 as v
class Numeric312Evidence(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(v.SPEC.read_text());self.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  edit(self.bundle)
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def event(self,name,scene=0):return next(x for x in self.bundle[0]['rows']if x['event']==name and x.get('scene')==scene)
 def test_frozen_contract(self):self.assertEqual(v.validate(self.spec,self.bundle)['scalar_words'],9600)
 def test_missing_control(self):self.reject(lambda b:b.pop())
 def test_hooked_control(self):self.reject(lambda b:b[2]['rows'].insert(1,dict(event='trace-end')))
 def test_changed_fine_pose(self):self.reject(lambda b:self.event('delta-leave')['m']['pos'].__setitem__(0,1))
 def test_changed_physical_radius(self):self.reject(lambda b:self.event('delta-leave')['m'].__setitem__('radius',0))
 def test_changed_authored_radius(self):self.reject(lambda b:self.event('authored-radius').__setitem__('radius',0))
 def test_changed_native_getter(self):self.reject(lambda b:self.event('public-coordinate').__setitem__('value',1))
 def test_lost_record(self):self.reject(lambda b:b[0]['rows'].remove(self.event('admit-enter')))
 def test_expected_pose_cannot_follow_engine(self):
  self.spec['states'][0][0]['fine'][0]^=1
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_authored_zero_cannot_be_sanitized(self):
  self.spec['states'][16][0]['authored']=0
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_missing_instruction_guard(self):
  self.spec['instructions'].pop(next(iter(self.spec['instructions'])))
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_changed_capture_source(self):self.reject(lambda b:b[0]['rows'][0]['source_sha256'].__setitem__('numeric312_probe.j','0'*64))
 def test_owner_environment_is_forbidden(self):self.reject(lambda b:b[0]['rows'][0].__setitem__('display',':97'))
if __name__=='__main__':unittest.main()
