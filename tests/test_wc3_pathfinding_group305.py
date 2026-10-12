"""Frozen binding evidence cannot be weakened by a registry optimization."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_group305 as v
class Group305Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  spec=copy.deepcopy(self.spec);edit(spec)
  with self.assertRaises(ValueError):v.validate(spec)
 def test_frozen_original_scopes_and_frida_repeats(self):v.validate(self.spec)
 def test_missing_original_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_missing_large_battle(self):self.reject(lambda s:s['unit_counts'].pop())
 def test_missing_queue_or_journey(self):self.reject(lambda s:s['engine_tests'].pop(2))
 def test_substituted_historical_header(self):self.reject(lambda s:s['pins'].__setitem__(v.SOURCES[-1],'0'*64))
 def test_save_layout_cannot_change_silently(self):self.reject(lambda s:s.__setitem__('save_version',184))
if __name__=='__main__':unittest.main()
