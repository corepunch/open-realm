"""Keep current-form Stop evidence and its observer-free control intact."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work258 as v
class StopFormTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.s=json.loads(v.FIXTURE.read_text());cls.b=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def test_complete_original_contract(self):self.assertEqual(len(v.validate(self.s)['original']),16)
 def test_missing_repeat_fails(self):
  b=copy.deepcopy(self.b);b['captures'].pop()
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_instrumented_control_fails(self):
  b=copy.deepcopy(self.b);b['captures'][2]['rows'].insert(1,dict(seq=0,event='unit-stop-enter'))
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_structure_cleanup_cannot_be_added(self):
  b=copy.deepcopy(self.b);next(r for r in b['captures'][0]['rows']if r.get('event')=='unit-stop-enter')['scene']=1
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_mobile_regions_cannot_be_dropped(self):
  b=copy.deepcopy(self.b);next(r for r in b['captures'][0]['rows']if r.get('event')=='widget-toggle-exit')['flags']=[]
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_public_control_cannot_change(self):
  b=copy.deepcopy(self.b);b['captures'][2]['preload']=b['captures'][2]['preload'].replace('accepted','rejected')
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_unpinned_sources_fail(self):
  s=copy.deepcopy(self.s);s['pins']={}
  with self.assertRaises(ValueError):v.validate(s)
if __name__=='__main__':unittest.main()
