"""Ordered admission fixtures must preserve original public and counter observations."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_pause289 as verifier
class Pause289Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(verifier.SPEC.read_text())
 def test_parent_control_and_both_counter_repeats(self):verifier.validate(self.spec)
 def test_changed_public_acceptance_rejected(self):
  spec=copy.deepcopy(self.spec);spec['projection']['9'][4][0]='nested-move-false'
  with self.assertRaises(ValueError):verifier.validate(spec)
 def test_erased_unvisited_owner_underflow_rejected(self):
  spec=copy.deepcopy(self.spec);row=next(r for r in spec['counter_projection']if r[2]=='nested-unpause');row[4]=[0,0]
  with self.assertRaises(ValueError):verifier.validate(spec)
 def test_callback_occurrence_cannot_be_conflated_with_activation(self):
  spec=copy.deepcopy(self.spec);spec['counter_projection'][2][1]=9
  with self.assertRaises(ValueError):verifier.validate(spec)
 def test_removed_successor_count_rejected(self):
  spec=copy.deepcopy(self.spec);spec['counts']['9']=2
  with self.assertRaises(ValueError):verifier.validate(spec)
 def test_changed_evidence_source_rejected(self):
  spec=copy.deepcopy(self.spec);spec['source_sha256'][verifier.SOURCES[0]]='0'*64
  with self.assertRaises(ValueError):verifier.validate(spec)
if __name__=='__main__':unittest.main()
