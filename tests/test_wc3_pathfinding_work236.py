"""Reject altered source readiness, candidate ordering and native expectations."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work236 as V
class Work236Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def publication(self,b):return next(r for r in b['captures'][0]['rows']if r.get('event')=='publish')
 def test_frozen_contract(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['source_publications'],2)
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop())
 def test_source_prematurely_ready(self):self.reject(lambda b:self.publication(b)['before'].__setitem__(0,[1109,1109]))
 def test_peer_prematurely_consumed(self):self.reject(lambda b:self.publication(b)['after'].__setitem__(1,None))
 def test_pending_result(self):self.reject(lambda b:self.publication(b).update(result=1))
 def test_missing_source_publication(self):
  self.reject(lambda b:b['captures'][0].update(rows=[r for r in b['captures'][0]['rows']if r.get('event')!='set-ready']))
 def test_invalid_scope_or_request(self):
  self.reject(lambda b:self.publication(b).update(callback=False))
  self.reject(lambda b:self.publication(b).update(request=[1,2]))
 def test_incomplete_or_unowned(self):
  self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
 def test_native_branch_domains(self):
  rows=self.spec['kernel']['rows'];self.assertEqual(len(rows),48)
  self.assertEqual({r['preferred']for r in rows},set(range(8)))
  cases={(r['case'],r['preferred']):r['groups']for r in rows}
  self.assertEqual(cases[0,0],[[0,1],[2]])
  self.assertEqual(cases[1,0],[[0,1,2]])
  self.assertEqual(cases[2,0],[[0,1,2]])
  self.assertNotEqual(cases[3,0],cases[3,7])
  self.assertTrue(all(r['ready']==[-1,1,2]and r['exclusions_restored']for r in rows))
if __name__=='__main__':unittest.main()
