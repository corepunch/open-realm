"""Attack profile evidence rejects missing owners, weakened controls and changed words."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_attack306 as v
class Attack306Evidence(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(v.SPEC.read_text());self.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  edit(self.bundle)
  with self.assertRaises(ValueError):v.validate_runtime(self.bundle,self.spec)
 def event(self,event,scene=None):return next(r for r in self.bundle['captures'][0]['rows']if r['event']==event and(scene is None or r.get('scene')==scene))
 def test_frozen_capture_provenance_and_exact_motion(self):self.assertEqual(v.validate(self.spec)['exact_chase_states'],62)
 def test_missing_owner_commit(self):self.reject(lambda b:b['captures'][0]['rows'].remove(self.event('commit-enter')))
 def test_changed_velocity_word(self):self.reject(lambda b:self.event('commit-enter')['g']['members'][0]['m']['vel'].__setitem__(0,1))
 def test_flyer_request_cannot_be_discarded(self):self.reject(lambda b:self.event('move-task',1).__setitem__('persistent',1))
 def test_structure_region_cannot_be_dropped(self):self.reject(lambda b:self.event('target-request',0).__setitem__('targetRegion','0x0'))
 def test_tree_point_control_cannot_be_promoted(self):self.reject(lambda b:self.event('bind',2)['g'].__setitem__('flags',0x1000))
 def test_repeated_member_must_retain_canonical_owner(self):self.reject(lambda b:self.event('commit-enter')['g']['members'][0]['m'].__setitem__('group',[0,0]))
 def test_public_control_is_required(self):self.reject(lambda b:b['captures'].pop())
 def test_hooked_control_is_rejected(self):self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='bind')))
 def test_missing_stop_or_pause_cannot_pass(self):self.reject(lambda b:b['captures'][0].__setitem__('preload',b['captures'][0]['preload'].replace('label=paused','label=changed')))
 def test_original_instruction_inventory_is_guarded(self):
  self.spec['instructions'].pop(next(iter(self.spec['instructions'])))
  with self.assertRaises(ValueError):v.validate(self.spec)
 def test_native_words_cannot_be_edited_to_match_engine(self):
  self.spec['motion'][0]['rows'][1][5]^=1
  with self.assertRaises(ValueError):v.validate(self.spec)
if __name__=='__main__':unittest.main()
