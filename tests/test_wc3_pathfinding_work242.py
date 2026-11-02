"""Preserve public queued identity evidence and reject omitted pending heads."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work242 as V
class Work242Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def test_frozen_contract(self):V.validate(self.spec);V.validate_runtime(self.bundle,self.spec)
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop())
 def test_missing_complete_markers(self):self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
 def test_omitted_pending_matching_head(self):self.reject(lambda b:[r for r in b['captures'][0]['rows']if r.get('event')=='candidate'][4]['row'].__setitem__(1,1))
 def test_omitted_pending_total_head(self):self.reject(lambda b:[r for r in b['captures'][0]['rows']if r.get('event')=='candidate'][4]['row'].__setitem__(2,1))
 def test_replacement_marked_shift(self):self.reject(lambda b:[r for r in b['captures'][0]['rows']if r.get('event')=='player-input'][1]['plan'].update(shift=True))
 def test_changed_score(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='score').update(value=0))
 def test_changed_publication(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='publish').update(unit='0x1'))
 def test_unbalanced_scope(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='trace-end').update(depth=1))
if __name__=='__main__':unittest.main()
