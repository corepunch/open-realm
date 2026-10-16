"""Reject altered point-query evidence without regenerating expected values."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work231 as V
class Work231Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def point(self,b):return next(r for r in b['captures'][0]['rows']if r.get('event')=='point')
 def test_frozen_literals_and_repeats(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['live_queries'],96)
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop())
 def test_boolean_and_mask(self):
  self.reject(lambda b:self.point(b).update(result=0))
  self.reject(lambda b:self.point(b).update(mask=2))
 def test_stamp_and_mode(self):
  self.reject(lambda b:self.point(b).update(stamp_after=0))
  self.reject(lambda b:self.point(b).update(mode_after=1))
 def test_completion_and_ownership(self):
  self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
 def test_control_markers(self):
  self.reject(lambda b:b['captures'][2].__setitem__('preload',b['captures'][2]['preload'].replace('bits=10010000','bits=10010001')))
 def test_native_windows(self):
  self.reject(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows']if r.get('event')=='marker'and ' begin='in r['value'])))
 def test_literal_expectations(self):
  s=copy.deepcopy(self.spec);s['kernels'][0]['stamp']=0
  with self.assertRaises(ValueError):V.validate(s)
if __name__=='__main__':unittest.main()
