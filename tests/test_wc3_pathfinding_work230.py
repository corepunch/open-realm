"""Reject incomplete or altered widget-bound evidence without regenerating it."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work230 as V
class Work230Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def bounds(self,b):return next(r for r in b['captures'][0]['rows']if r.get('event')=='region-bounds')
 def test_frozen_literals_and_repeats(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['live_bounds'],2)
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop())
 def test_bounds_words(self):
  self.reject(lambda b:self.bounds(b)['bounds'].__setitem__(0,0))
  self.reject(lambda b:self.bounds(b)['boxes'][0].__setitem__(2,30))
 def test_origin_and_collection(self):
  self.reject(lambda b:self.bounds(b)['origin'].__setitem__(1,0x80000000))
  self.reject(lambda b:self.bounds(b)['boxes'].pop())
 def test_completion_and_ownership(self):
  self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
 def test_control_markers(self):
  self.reject(lambda b:b['captures'][2].__setitem__('preload',b['captures'][2]['preload'].replace('x=192.000','x=193.000')))
 def test_query_failure_and_alias_slots(self):
  s=copy.deepcopy(self.spec);s['scopes'][4]['result']=0
  with self.assertRaises(ValueError):V.validate(s)
  s=copy.deepcopy(self.spec);s['scopes'][2]['alias_events'].pop()
  with self.assertRaises(ValueError):V.validate(s)
if __name__=='__main__':unittest.main()
