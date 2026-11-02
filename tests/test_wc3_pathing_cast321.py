"""Reject incomplete casts, altered native phases and weakened command results."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from verify_wc3_pathing_cast321 import SPEC,validate
class Cast321Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(SPEC.read_text());cls.bundle=json.loads(gzip.decompress(SPEC.with_suffix('.json.gz').read_bytes()))
 def test_complete_public_cast_and_motion(self):
  result=validate(self.spec,self.bundle)
  self.assertEqual(result['motion_rows'],861);self.assertEqual(result['caster_commits'],4)
 def test_both_repeats_and_unhooked_control_required(self):
  for index in range(3):
   changed=copy.deepcopy(self.bundle);changed.pop(index)
   with self.assertRaises(ValueError):validate(self.spec,changed)
 def test_effect_clock_cannot_be_rebased(self):
  changed=copy.deepcopy(self.bundle)
  next(x for x in changed[0]['rows']if x.get('event')=='spell-effect')['primary'][0]^=1
  with self.assertRaisesRegex(ValueError,'cast phase differs'):validate(self.spec,changed)
 def test_noop_autocast_return_is_rejection(self):
  changed=copy.deepcopy(self.bundle)
  next(x for x in changed[0]['rows']if x.get('event')=='immediate-order-result')['result']=1
  with self.assertRaisesRegex(ValueError,'public command acceptance'):validate(self.spec,changed)
 def test_facing_request_retains_infinite_range(self):
  changed=copy.deepcopy(self.bundle)
  next(x for x in changed[0]['rows']if x.get('event')=='spell-target-task')['range']=0x442f0000
  with self.assertRaisesRegex(ValueError,'turn admission'):validate(self.spec,changed)
 def test_missing_native_events_are_rejected(self):
  changed=copy.deepcopy(self.bundle)
  next(x for x in changed[0]['rows']if x.get('event')=='spell-cast')['seq']+=1
  with self.assertRaisesRegex(ValueError,'lost events'):validate(self.spec,changed)
if __name__=='__main__':unittest.main()
