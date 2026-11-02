"""Reject incomplete unit/bridge scope evidence without changing retail fixtures."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work257 as v
class ScopeTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.s=json.loads(v.FIXTURE.read_text());cls.b=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def test_complete_original_contract(self):self.assertEqual(len(v.validate(self.s)['original']['rows']),16)
 def test_missing_unit_boundary_fails(self):
  b=copy.deepcopy(self.b);del b['captures'][0]['rows'][next(i for i,r in enumerate(b['captures'][0]['rows'])if r.get('event')=='unit-toggle-enter')]
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_incorrect_bridge_depth_fails(self):
  b=copy.deepcopy(self.b);next(r for r in b['captures'][0]['rows']if r.get('event')=='stop-mover-enter')['counter']=1
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_incomplete_repeat_fails(self):
  b=copy.deepcopy(self.b);b['captures'].pop()
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_instrumented_control_provenance_fails(self):
  b=copy.deepcopy(self.b);b['captures'][0]['rows'][0]['mode']='control'
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_public_motion_cannot_change(self):
  b=copy.deepcopy(self.b);b['captures'][0]['preload']=b['captures'][0]['preload'].replace('656.000','657.000')
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_unpinned_sources_fail(self):
  s=copy.deepcopy(self.s);s['pins']={}
  with self.assertRaises(ValueError):v.validate(s)
if __name__=='__main__':unittest.main()
