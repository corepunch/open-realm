"""Protect native three-request affinity and preserve the UI-sort evidence limit."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work240 as V
class Work240Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  s=copy.deepcopy(self.spec);change(s)
  with self.assertRaises(ValueError):V.validate(s)
 def reject_live(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def test_frozen_native_and_public(self):V.validate(self.spec);V.validate_runtime(self.bundle,self.spec)
 def test_missing_native_case(self):self.reject(lambda s:s['kernel']['rows'].pop())
 def test_float_routed_as_ground(self):self.reject(lambda s:s['kernel']['rows'][-1]['slots'].__setitem__(0,0))
 def test_wrong_readiness(self):self.reject(lambda s:s['kernel']['rows'][-1]['births'][0].update(after=0))
 def test_missing_source(self):self.reject(lambda s:s['pins'].__setitem__(V.SOURCES[0],'0'*64))
 def test_missing_save_regression(self):self.reject(lambda s:s['engine_tests'].pop(1))
 def test_missing_native_instruction(self):self.reject(lambda s:s['instructions'].pop('6f6b8c10'))
 def test_wrong_public_float_attachment(self):self.reject_live(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='attach').update(type=16))
 def test_lost_public_scope(self):self.reject_live(lambda b:b['captures'][2]['rows'][-1].update(complete=False))
 def test_changed_public_publication_order(self):
  def swap(b):
   r=b['captures'][2]['rows'];indices=[i for i,x in enumerate(r)if x.get('event')=='bind' and x['depth']==0];a,c=indices[:2];r[a],r[c]=r[c],r[a]
  self.reject_live(swap)
if __name__=='__main__':unittest.main()
