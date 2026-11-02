"""Reject altered native group-route caches, work accounting and control evidence."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work234 as V
class Work234Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def disabled(self,b):return next(r for r in b['captures'][0]['rows']if r.get('event')=='route'and r['phase']==2)
 def test_frozen_contract(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['disabled_requests'],2)
 def test_missing_repeat_or_control(self):
  self.reject(lambda b:b['captures'].pop());self.reject(lambda b:b['captures'].pop(1))
 def test_dropped_cache(self):self.reject(lambda b:self.disabled(b)['after'].update(count=0))
 def test_wrong_endpoint(self):self.reject(lambda b:self.disabled(b)['after']['points'].__setitem__(0,0))
 def test_wrong_index(self):self.reject(lambda b:self.disabled(b)['after'].update(index=0xffffffff))
 def test_consumed_admission(self):
  self.reject(lambda b:self.disabled(b).update(searches=1))
  self.reject(lambda b:self.disabled(b)['after']['timestamps'].__setitem__(1,42))
 def test_incomplete_or_unowned(self):
  self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
 def test_hooked_control(self):self.reject(lambda b:b['captures'][2]['rows'].insert(1,copy.deepcopy(self.disabled(b))))
 def test_original_replaces_all_histories(self):
  rows=self.spec['kernel']['rows'];self.assertEqual(len(rows),36)
  self.assertEqual({r['history']for r in rows},{0,1,3})
  self.assertTrue(all(r['count']==1 and r['index']==0 and r['capacity']==128 and r['timestamps']==[123,456]for r in rows))
if __name__=='__main__':unittest.main()
