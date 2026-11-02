"""Boundary evidence must reject missing timeline/state/source instructions."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_boundary261 as v
class BoundaryTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(v.FIXTURE.read_text())
 def test_complete_contract(self):self.assertEqual(len(v.validate(self.spec)['original']['rows']),80)
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_clock_word_changes_rejected(self):self.reject(lambda s:s['original']['rows'][25].update(time=0))
 def test_queue_count_changes_rejected(self):self.reject(lambda s:s['original']['rows'][23]['buckets'][3].__setitem__(2,2))
 def test_timeline_truncation_rejected(self):self.reject(lambda s:s['original']['rows'].pop())
 def test_missing_instruction_rejected(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_source_pin_changes_rejected(self):self.reject(lambda s:s['pins'].__setitem__(v.SOURCES[0],'0'*64))
 def test_missing_engine_continuation_rejected(self):self.reject(lambda s:s['engine_tests'].pop())
 def test_empty_provenance_rejected(self):self.reject(lambda s:s.update(pins={}))
if __name__=='__main__':unittest.main()
