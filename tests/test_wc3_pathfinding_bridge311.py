"""Retail bridge positions and retained state cannot be rewritten to follow the engine."""
import gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_bridge311 as v
class Bridge311Evidence(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(v.SPEC.read_text());self.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  edit(self.bundle)
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def event(self,name,scene=0):return next(x for x in self.bundle['base'][0]['rows']if x['event']==name and x.get('scene')==scene)
 def test_frozen_contract(self):self.assertEqual(v.validate(self.spec)['placement_states'],534)
 def test_missing_control(self):self.reject(lambda b:b['base'].pop())
 def test_hooked_control(self):self.reject(lambda b:b['base'][2]['rows'].insert(1,dict(event='trace-end')))
 def test_changed_fine_pose(self):self.reject(lambda b:self.event('delta-leave')['m']['pos'].__setitem__(0,1))
 def test_changed_admission_result(self):self.reject(lambda b:self.event('admit-leave').__setitem__('result',0))
 def test_changed_level_rectangle(self):self.reject(lambda b:self.event('deck-registration',-1)['entries'][0]['header'].__setitem__(3,13))
 def test_changed_provider_delta(self):self.reject(lambda b:self.event('deck-level-delta',168).__setitem__('result',5))
 def test_lost_record(self):self.reject(lambda b:b['base'][0]['rows'].remove(self.event('admit-enter')))
 def test_changed_grid_value(self):
  self.reject(lambda b:next(x for x in b['grids'][0]['rows']if x['event']=='presentation-grid')['values'].__setitem__(0,1))
 def test_changed_retirement_patch(self):
  self.reject(lambda b:next(x for x in b['lifetimes'][0]['rows']if x['event']=='diagnostic-patch')['cells'][0]['records'][0].__setitem__('live',-1))
 def test_missing_retirement_repeat(self):self.reject(lambda b:b['lifetimes'].pop())
 def test_missing_grid_repeat(self):self.reject(lambda b:b['grids'].pop())
 def test_expected_pose_cannot_follow_engine(self):
  self.spec['states'][0][0]['fine'][0]^=1
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_observed_pose_variation_cannot_be_erased(self):
  self.spec['lifetime_levels'][0][2]=9
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_missing_instruction_guard(self):
  self.spec['instructions'].pop(next(iter(self.spec['instructions'])))
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def test_changed_capture_source(self):self.reject(lambda b:b['base'][0]['rows'][0]['source_sha256'].__setitem__('bridge311_probe.j','0'*64))
 def test_owner_environment_is_forbidden(self):self.reject(lambda b:b['base'][0]['rows'][0].__setitem__('display',':97'))
if __name__=='__main__':unittest.main()
