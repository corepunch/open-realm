"""Reject altered death/removal identity, publication and counter evidence."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_scope301 as v
class Scope301Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  spec=copy.deepcopy(self.spec);edit(spec)
  with self.assertRaises(ValueError):v.validate(spec)
 def test_original_unchanged_spatial_repeats(self):v.validate(self.spec)
 def test_substituted_evidence(self):self.reject(lambda s:s.__setitem__('evidence_sha256','0'*64))
 def test_missing_production_test(self):self.reject(lambda s:s['engine_tests'].pop())
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_wrong_inactive_constant(self):self.reject(lambda s:s['instructions'].__setitem__('6fce4588','00'*16))
 def test_wrong_global_type(self):self.reject(lambda s:s.__setitem__('global_type','float[4]'))
 def runtime_reject(self,event,scene,occurrence,edit):
  _,_,b=v.validate(self.spec)
  rows=[r for r in b['captures'][0]['rows']if r.get('event')==event and r.get('scene')==scene]
  edit(rows[occurrence])
  with self.assertRaises(ValueError):v.validate_runtime(b)
 def test_death_fine_identity_not_reallocated(self):
  self.runtime_reject('inner-enter',6,2,lambda r:r['fine']['identity'].__setitem__(1,0))
 def test_removal_unpublishes_before_nested_recovery(self):
  self.runtime_reject('inner-enter',4,1,lambda r:r['fine'].__setitem__('category',0x010000ca))
 def test_death_keeps_active_coverage_during_recovery(self):
  self.runtime_reject('inner-enter',14,3,lambda r:r['fine'].__setitem__('box',v.EMPTY))
 def test_inactive_box_is_exact(self):
  self.runtime_reject('marker-spatial',12,1,lambda r:r['fine']['box'].__setitem__(0,-127999))
 def test_death_record_survives_settled_marker(self):
  self.runtime_reject('marker-spatial',6,4,lambda r:r.__setitem__('fine',None))
 def test_removed_record_is_physically_released(self):
  self.runtime_reject('marker-spatial',4,4,lambda r:r.__setitem__('fine',{}))
 def test_extra_destruction_recovery_rejected(self):
  _,_,bundle=v.validate(self.spec);rows=bundle['captures'][0]['rows']
  rows.append(copy.deepcopy(next(r for r in rows if r.get('event')=='unit-stop-enter' and r.get('scene')==4)))
  with self.assertRaises(ValueError):v.validate_runtime(bundle)
 def test_exclusion_balance(self):
  self.runtime_reject('marker-spatial',14,3,lambda r:r['fine'].__setitem__('flags',1))
if __name__=='__main__':unittest.main()
