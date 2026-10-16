"""Retail Build expectations must remain independent of engine output."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_build293 as v
class Build293Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_repeats_control_and_five_styles(self):v.validate(self.spec)
 def test_changed_public_head(self):self.reject(lambda s:s['public'].__setitem__(0,'B293 changed'))
 def test_source_substitution(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCES[0],'0'*64))
 def test_missing_native_boundary(self):self.reject(lambda s:s['native'].pop())
 def test_wrong_build_acceptance(self):
  def edit(s):next(x for x in s['native']if x['event']=='build-native-leave')['accepted']=0
  self.reject(edit)
 def test_changed_repair_owner(self):
  def edit(s):next(x for x in s['native']if x['event']=='repair-work-leave')['unit']['head']=[0,0]
  self.reject(edit)
 def test_bundle_substitution(self):self.reject(lambda s:s.__setitem__('bundle_sha256','0'*64))
 def test_map_substitution(self):self.reject(lambda s:s.__setitem__('map_sha256','0'*64))
 def test_missing_instruction_guard(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
if __name__=='__main__':unittest.main()
