"""Reject substitutions of retail CTownSpot defaults and ordered reset evidence."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_captain291 as v
class Captain291Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_repeats_control_words_and_order(self):v.validate(self.spec)
 def test_changed_public_member_trajectory(self):self.reject(lambda s:s['public'].__setitem__(0,'R291 changed'))
 def test_source_substitution(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCES[0],'0'*64))
 def test_omitted_native_boundary(self):self.reject(lambda s:s['native'].pop())
 def test_single_home_instead_of_distinct_defaults(self):
  def edit(s):
   row=next(x for x in s['native']if x['event']=='location-refresh'and x['after']['attack']!=x['after']['defense'])
   row['after']['attack']=row['after']['defense'][:]
  self.reject(edit)
 def test_relocated_actor_pose_substitution(self):
  def edit(s):next(x for x in s['native']if x['event']=='teleport-leave')['after']['actor']['position']=[0,0]
  self.reject(edit)
 def test_created_actor_state_substitution(self):
  def edit(s):next(x for x in s['native']if x['event']=='actor-create')['captain']['state']=0
  self.reject(edit)
 def test_nonplanar_original_input_substitution(self):self.reject(lambda s:s['cases'][13]['building'].__setitem__(2,0))
 def test_original_word_substitution(self):self.reject(lambda s:s['original'][13]['after'].__setitem__(0,0))
 def test_capture_bundle_substitution(self):self.reject(lambda s:s.__setitem__('bundle_sha256','0'*64))
 def test_observed_map_substitution(self):self.reject(lambda s:s.__setitem__('map_sha256','0'*64))
if __name__=='__main__':unittest.main()
