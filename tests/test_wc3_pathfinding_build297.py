"""Reject changes to original Haunted Mine continuation evidence."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_build297 as v
class Build297Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit,semantic=False):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):(v.validate_semantics if semantic else v.validate)(s)
 def test_original_repeats_and_control(self):v.validate(self.spec)
 def test_source_substitution(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCES[0],'0'*64))
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_changed_bundle(self):self.reject(lambda s:s.__setitem__('bundle_sha256','0'*64))
 def test_missing_public_stage(self):self.reject(lambda s:s['public'].pop())
 def test_changed_native_entry(self):self.reject(lambda s:s['native'].pop())
 def test_smart_is_not_harvest(self):
  self.reject(lambda s:next(x for x in s['native']if x['event']=='completion-order-create').__setitem__('code',851971),True)
 def test_stop_retains_completion(self):
  self.reject(lambda s:next(x for x in s['native']if x['event']=='complete-enter'and x['scene']==3).__setitem__('retained',v.INVALID),True)
 def test_move_cancels_completion(self):
  self.reject(lambda s:next(x for x in s['native']if x['event']=='complete-enter'and x['scene']==2).__setitem__('retained',[895,1306]),True)
 def test_removed_ability_keeps_retained_identity(self):
  self.reject(lambda s:next(x for x in s['native']if x['event']=='complete-leave'and x['scene']==9).__setitem__('retained',v.INVALID),True)
 def test_computer_controller_did_not_prove_ai_flag(self):
  def edit(s):next(x for x in s['native']if x['event']=='complete-enter'and x['scene']==7)['unit']['flags']|=4
  self.reject(edit,True)
if __name__=='__main__':unittest.main()
