"""Keep original counted removal evidence separate from engine expectations."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_separation302 as v
class Separation302Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_complete_original_repeats_and_control(self):v.validate(self.spec)
 def test_evidence_cannot_be_replaced(self):self.reject(lambda s:s.__setitem__('evidence_sha256','0'*64))
 def test_source_cannot_be_replaced(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCE,'0'*64))
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_missing_function(self):self.reject(lambda s:s.__setitem__('function_count',8))
 def test_missing_engine_scope(self):self.reject(lambda s:s['engine_tests'].pop())
 def test_old_save_contract_rejected(self):self.reject(lambda s:s.__setitem__('save_version',184))
 def test_removal_count_cannot_be_omitted(self):self.reject(lambda s:s['retail_transitions'][0]['steps'][0].__setitem__(1,0))
 def test_final_inverse_cannot_be_omitted(self):self.reject(lambda s:s['retail_transitions'][1]['steps'].pop())
 def test_paused_inverse_cannot_be_collapsed(self):self.reject(lambda s:s['retail_transitions'][2]['steps'].pop(0))
 def test_owner_at_final_configuration_is_current(self):self.reject(lambda s:s['retail_transitions'][0]['steps'][1].__setitem__(3,0x10110000))
 def test_paused_state_must_clear_before_new_acquire(self):self.reject(lambda s:s['retail_transitions'][2]['steps'][1].__setitem__(2,1))
 def test_original_transitions_reject_extra_counted_owner(self):
  b=v.validate(self.spec)
  row=next(r for r in b['stages']if r['event']=='acquire-end')
  row['unit']['depth']+=1
  with self.assertRaises(ValueError):v.transitions(b)
if __name__=='__main__':unittest.main()
