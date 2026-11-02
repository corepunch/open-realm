"""Reject changed cohort evidence without rewriting retail expectations."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work232 as V
class Work232Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def row(self,b,event):return next(r for r in b['captures'][0]['rows']if r.get('event')==event)
 def test_frozen_contract(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['live_queries'],4)
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop())
 def test_wrong_selector(self):self.reject(lambda b:self.row(b,'query')['words'].__setitem__(2,9))
 def test_wrong_radius(self):self.reject(lambda b:self.row(b,'query')['words'].__setitem__(3,1103101952))
 def test_wrong_history(self):self.reject(lambda b:self.row(b,'candidate')['previous'].__setitem__(0,0))
 def test_wrong_type(self):self.reject(lambda b:self.row(b,'candidate').update(category=4))
 def test_stop_and_scope(self):
  self.reject(lambda b:self.row(b,'candidate').update(result=0))
  self.reject(lambda b:self.row(b,'cohort').update(result=1))
 def test_incomplete_or_unowned(self):
  self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
 def test_original_boundary(self):
  rows=self.spec['kernel']['rows'];self.assertEqual(rows[2]['candidates'],[2,1,0]);self.assertEqual(rows[4]['candidates'],[2,1])
  self.assertEqual(rows[3]['candidates'],[2]);self.assertEqual(rows[3]['materialized'],3)
if __name__=='__main__':unittest.main()
