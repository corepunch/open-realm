"""Reject substituted evidence and omitted suspension cleanup owners."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_scope300 as v
class Scope300Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_original_repeats_and_unchanged_control(self):v.validate(self.spec)
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_changed_observer_source(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCES[0],'0'*64))
 def test_substituted_bundle(self):self.reject(lambda s:s.__setitem__('bundle_sha256','0'*64))
 def test_missing_production_tests(self):self.reject(lambda s:s['engine_tests'].pop(0))
 def test_wrong_original_binary(self):self.reject(lambda s:s.__setitem__('game_sha256','0'*64))
 def runtime_reject(self,edit):
  old,ob,b=v.validate(self.spec);edit(b)
  with self.assertRaises(ValueError):v.validate_runtime(b,old,ob,self.spec)
 def test_same_identity_cannot_hide_inactive_fine_object(self):
  def edit(b):
   r=next(r for r in b['captures'][0]['rows']if r.get('scene')==5 and r.get('event')=='inner-enter');r['fine']['category']&=~0x01000000
  self.runtime_reject(edit)
 def test_reallocated_fine_identity_rejected(self):
  def edit(b):
   r=next(r for r in b['captures'][0]['rows']if r.get('scene')==13 and r.get('event')=='inner-enter');r['fine']['identity'][0]+=1
  self.runtime_reject(edit)
 def test_unbalanced_exclusion_rejected(self):
  def edit(b):
   r=next(r for r in b['captures'][0]['rows']if r.get('scene')==5 and r.get('event')=='marker-spatial');r['fine']['flags']=1
  self.runtime_reject(edit)
 def test_control_cannot_be_replaced_by_an_observed_run(self):
  self.runtime_reject(lambda b:b['captures'].__setitem__(2,copy.deepcopy(b['captures'][0])))
if __name__=='__main__':unittest.main()
