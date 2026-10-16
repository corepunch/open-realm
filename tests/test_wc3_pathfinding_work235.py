"""Reject rewritten native cohorts, recursive ordering and observer controls."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work235 as V
class Work235Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def bind(self,b):return next(r for r in b['captures'][0]['rows']if r.get('event')=='bind'and r['depth']==0)
 def test_frozen_contract(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['live_binds'],18)
 def test_missing_repeat_or_control(self):
  self.reject(lambda b:b['captures'].pop());self.reject(lambda b:b['captures'].pop(1))
 def test_dropped_member(self):self.reject(lambda b:self.bind(b)['members'].pop())
 def test_reordered_recursive_members(self):self.reject(lambda b:self.bind(b)['members'].reverse())
 def test_changed_preference(self):self.reject(lambda b:self.bind(b)['candidates'][0].update(path=0))
 def test_changed_prediction(self):self.reject(lambda b:self.bind(b)['candidates'][0]['pose'].__setitem__(2,1))
 def test_incomplete_or_unowned(self):
  self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
 def test_hooked_control(self):self.reject(lambda b:b['captures'][2]['rows'].insert(1,copy.deepcopy(self.bind(b))))
 def test_native_branch_domains(self):
  rows=self.spec['kernel']['rows'];self.assertEqual(len(rows),192)
  self.assertEqual({r['flags']for r in rows},{0,0x100,0x200})
  self.assertEqual({r['preferred']for r in rows},set(range(8)))
  self.assertTrue(all(r['ready']==[0xffffffff]*3 and r['mover_flags']==[0xab001234]*3 for r in rows))
  cases={(r['case'],r['flags'],r['preferred']):r['groups']for r in rows}
  self.assertEqual(cases[6,0,0],[[0,2,1]])
  self.assertEqual(cases[2,0,0],[[0],[1],[2]])
  self.assertEqual(cases[2,0,7],[[0,1],[2]])
  self.assertNotEqual(cases[7,0,0],cases[7,0,7])
if __name__=='__main__':unittest.main()
