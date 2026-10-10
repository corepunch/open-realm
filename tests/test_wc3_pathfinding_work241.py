"""Reject mutations of original comparator, candidate keys and ordered UI publication."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work241 as V
class Work241Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  s=copy.deepcopy(self.spec);change(s)
  with self.assertRaises(ValueError):V.validate(s)
 def reject_live(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def test_frozen_contract(self):V.validate(self.spec);V.validate_runtime(self.bundle,self.spec)
 def test_missing_native_pair(self):self.reject(lambda s:s['kernel']['cases'].pop())
 def test_changed_native_result(self):self.reject(lambda s:s['kernel']['cases'][0].update(result=0))
 def test_missing_engine_priority_test(self):self.reject(lambda s:s['engine_tests'].pop(0))
 def test_changed_observer_pin(self):self.reject(lambda s:s['pins'].__setitem__(V.SOURCES[4],'0'*64))
 def test_missing_capture(self):self.reject_live(lambda b:b['captures'].pop())
 def test_incomplete_public_markers(self):self.reject_live(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
 def test_changed_current_order_count(self):
  self.reject_live(lambda b:next(r for r in b['captures'][2]['rows']if r.get('event')=='candidate').update(unit1b4=7))
 def test_changed_score(self):self.reject_live(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='score').update(value=0))
 def test_changed_pose(self):self.reject_live(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='score-pose')['words'].__setitem__(0,0))
 def test_changed_subgroup(self):self.reject_live(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='candidate')['row'].__setitem__(5,0))
 def test_changed_callback_order(self):
  def swap(b):
   r=b['captures'][0]['rows'];ids=[i for i,x in enumerate(r)if x.get('event')=='publish'];a,c=ids[:2];r[a],r[c]=r[c],r[a]
  self.reject_live(swap)
 def test_changed_member_order(self):self.reject_live(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='bind'and len(r['members'])==3)['members'].reverse())
 def test_unbalanced_observer(self):self.reject_live(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='trace-end').update(dispatch=1))
if __name__=='__main__':unittest.main()
