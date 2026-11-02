"""Keep original visibility branches and optional Move normalization immutable."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work251 as v
class Work251Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def test_original_repeats_and_unhooked_control(self):
  self.assertEqual(v.verify_runtime(self.bundle,self.spec),dict(captures=2,controls=1,public_markers=117))
 def test_sources_original_matrix_and_branch_table(self):
  self.assertEqual(len(v.validate(self.spec)['visibility']['cases']),6912)
  self.assertEqual(v.verify_branches(),36)
 def test_rejects_identity_retained_after_invalid_move(self):
  b=copy.deepcopy(self.bundle)
  next(r for r in b['captures'][0]['rows']if r['event']=='target-order')['target']=123
  with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)
 def test_rejects_changed_admission_result(self):
  b=copy.deepcopy(self.bundle)
  next(r for r in b['captures'][1]['rows']if r['event']=='admission')['result']=0
  with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)
 def test_rejects_partial_capture(self):
  b=copy.deepcopy(self.bundle);b['captures'][0]['rows'][-1]['complete']=False
  with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)
 def test_rejects_hooked_control(self):
  b=copy.deepcopy(self.bundle);b['captures'][2]['rows'].insert(1,dict(event='query'))
  with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)
 def test_rejects_changed_preload_bytes(self):
  b=copy.deepcopy(self.bundle);b['captures'][0]['preload']=b['captures'][0]['preload'].replace('\r\n','\n')
  with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)
 def test_rejects_incomplete_original_matrix(self):
  b=copy.deepcopy(self.bundle['visibility']);b['cases'].pop()
  with self.assertRaises(ValueError):v.verify_matrix(b,(ROOT/v.HEADER).read_text())
 def test_rejects_header_that_ignores_detection(self):
  h=(ROOT/v.HEADER).read_text();h=h.replace('    {','    {9,',1)
  with self.assertRaises(ValueError):v.verify_matrix(self.bundle['visibility'],h)
if __name__=='__main__':unittest.main()
