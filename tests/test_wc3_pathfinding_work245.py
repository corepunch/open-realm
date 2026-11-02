"""Keep retail policy/facing evidence independent of engine outputs."""
import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_work245 as V
class Work245Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError,StopIteration)):V.validate_runtime(b,self.spec)
 def test_complete_captures(self):V.validate_runtime(self.bundle,self.spec)
 def test_policy_inventory(self):V.validate_inventory(self.bundle,self.spec)
 def test_literal_header_from_retail(self):self.assertEqual(V.header(self.bundle['captures'][0]['rows'],self.spec['facing_queries']),V.HEADER.read_text())
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop())
 def test_incomplete_capture(self):self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
 def test_changed_sentinel(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='target-request').update(range=0x7cffffff))
 def test_changed_policy(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='publish').update(flags=0x1000))
 def test_changed_heading(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='motion')['after']['pose'].__setitem__(5,0))
 def test_changed_clock(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='motion')['after']['clock'].__setitem__(0,0))
 def test_changed_preload(self):self.reject(lambda b:b['captures'][2].update(preload=''))
 def test_hooked_control(self):self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='trace-end')))
 def test_unbalanced_observer(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='trace-end').update(readOnly=False))
 def test_spacing_mutation(self):
  b=copy.deepcopy(self.bundle);b['constants']['flag20_pairs']['rank_gap'][0]='0x40b00000'
  with self.assertRaises(ValueError):V.validate_inventory(b,self.spec)
 def test_fabricated_spacing_producer(self):
  b=copy.deepcopy(self.bundle);b['references']['setters']['6f16d7e0']['absolute'].append(['.text','6f000000'])
  with self.assertRaises(ValueError):V.validate_inventory(b,self.spec)
if __name__=='__main__':unittest.main()
