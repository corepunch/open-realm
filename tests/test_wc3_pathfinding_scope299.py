"""Protect unchanged retail evidence and the separate ordinary Move cleanup scopes."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_scope299 as v
class Scope299Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_unchanged_retail_repeats_control_and_header(self):v.validate(self.spec)
 def test_replaced_evidence(self):self.reject(lambda s:s.__setitem__('evidence_sha256','0'*64))
 def test_missing_ghidra_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_source_substitution(self):self.reject(lambda s:s['source_sha256'].__setitem__('tools/ghidra/research/Scope299Evidence.java','0'*64))
 def test_missing_production_regression(self):self.reject(lambda s:s['engine_tests'].pop(0))
 def test_wrong_original_binary(self):self.reject(lambda s:s.__setitem__('game_sha256','0'*64))
if __name__=='__main__':unittest.main()
