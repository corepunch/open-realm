"""Retained-packet evidence must continue to match the unmodified retail capture."""
import copy,importlib.util,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_pause288 as verifier
class Pause288Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(verifier.SPEC.read_text())
 def test_frozen_parent_and_all_six_projections(self):verifier.validate(self.spec)
 def test_changed_expected_order_is_rejected(self):
  spec=copy.deepcopy(self.spec);spec['projection']['0'][0][1]='999999'
  with self.assertRaises(ValueError):verifier.validate(spec)
 def test_changed_stance_speed_is_rejected(self):
  spec=copy.deepcopy(self.spec);spec['projection']['2'][0][6]='270.000'
  with self.assertRaises(ValueError):verifier.validate(spec)
 def test_changed_capture_parent_is_rejected(self):
  spec=copy.deepcopy(self.spec);spec['parent_sha256']='0'*64
  with self.assertRaises(ValueError):verifier.validate(spec)
 def test_changed_original_source_is_rejected(self):
  spec=copy.deepcopy(self.spec);spec['source_sha256'][verifier.SOURCES[0]]='0'*64
  with self.assertRaises(ValueError):verifier.validate(spec)
if __name__=='__main__':unittest.main()
