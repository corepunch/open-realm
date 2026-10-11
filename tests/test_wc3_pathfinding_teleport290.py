"""Reject changes to observed relocation, ownership and observer controls."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_teleport290 as v
class Teleport290Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_repeat_control_and_all_native_boundaries(self):v.validate(self.spec)
 def test_changed_public_actor_query_rejected(self):self.reject(lambda s:s['public'].__setitem__(0,'R290 changed'))
 def test_velocity_clear_rejected(self):self.reject(lambda s:s['native'][1]['after']['actor'].__setitem__('velocity',[0,0]))
 def test_new_group_identity_rejected(self):self.reject(lambda s:s['native'][1]['after']['actor'].__setitem__('group',[0,0]))
 def test_unadmitted_requested_point_rejected(self):self.reject(lambda s:s['native'][3]['after']['actor'].__setitem__('position',[0,0]))
 def test_stop_recovery_rejected(self):self.reject(lambda s:s['native'][1].__setitem__('recovery_calls',1))
 def test_source_substitution_rejected(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCES[0],'0'*64))
if __name__=='__main__':unittest.main()
