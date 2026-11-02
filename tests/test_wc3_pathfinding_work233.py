"""Reject changed retail source-selection evidence and missing observations."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work233 as V
class Work233Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def row(self,b,event):return next(r for r in b['captures'][0]['rows']if r.get('event')==event)
 def test_frozen_contract(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['live_selections'],12)
 def test_missing_repeat_or_control(self):
  self.reject(lambda b:b['captures'].pop());self.reject(lambda b:b['captures'].pop(1))
 def test_wrong_source(self):self.reject(lambda b:self.row(b,'source').update(index=1))
 def test_changed_preferred_policy(self):
  self.reject(lambda b:self.row(b,'source')['members'][0].update(flags=0))
 def test_changed_prediction(self):
  self.reject(lambda b:self.row(b,'source')['members'][0]['velocity'].__setitem__(0,1065353216))
 def test_wrong_goal_or_bypass(self):
  self.reject(lambda b:self.row(b,'source')['goal'].__setitem__(0,0))
  self.reject(lambda b:self.row(b,'source').update(flags=0x200))
 def test_incomplete_or_unowned(self):
  self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
 def test_control_must_be_unhooked(self):
  self.reject(lambda b:b['captures'][2]['rows'].insert(1,copy.deepcopy(self.row(b,'source'))))
 def test_original_matrix(self):
  rows=self.spec['kernel']['rows'];self.assertEqual(len(rows),48)
  self.assertTrue(all(r['index']==0 for r in rows if r['flags']==0x200))
  self.assertEqual([r['index']for r in rows if r['flags']==0 and r['preferred']==0],[2,0,0])
if __name__=='__main__':unittest.main()
