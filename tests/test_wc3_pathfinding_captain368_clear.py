"""Reject reordered, incomplete or foreign ClearCaptainTargets evidence."""
import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_captain368_clear as v
class Captain368ClearTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.original=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.SPEC.with_suffix('.json.gz').read_bytes()))
 def setUp(self):self.spec=copy.deepcopy(self.original);self.groups=copy.deepcopy(self.bundle)
 def reject(self):
  with self.assertRaises(ValueError):v.runtime(self.groups,self.spec)
 def rows(self,scene='point'):return self.groups[scene][0]['rows']
 def clear(self,scene='point'):return next(r for r in self.rows(scene)if r.get('name')=='clear-targets')
 def test_completed_repeats_controls(self):self.assertEqual(v.runtime(self.groups,self.spec)['clear_boundaries'],4)
 def test_missing_repeat(self):self.groups['point'].pop();self.reject()
 def test_partial_capture(self):self.rows().pop();self.reject()
 def test_unowned_process(self):self.rows()[0]['owned']=False;self.reject()
 def test_foreign_environment(self):self.rows()[0]['env']='A';self.reject()
 def test_wrong_map(self):self.rows()[0]['source_sha256']['map']='0'*64;self.reject()
 def test_instrumented_control(self):self.groups['point'][2]['rows'].insert(1,{'event':'captain-call'});self.reject()
 def test_missing_role_callback(self):
  rows=self.rows();end=rows.index(self.clear());i=next(i for i,r in enumerate(rows[:end])if r.get('caller')==v.DIRECT_CALLERS[0]);rows.pop(i);self.reject()
 def test_reordered_role_callbacks(self):
  rows=self.rows();a=next(i for i,r in enumerate(rows)if r.get('caller')==v.DIRECT_CALLERS[0]);b=next(i for i,r in enumerate(rows)if r.get('caller')==v.DIRECT_CALLERS[1]);rows[a],rows[b]=rows[b],rows[a];self.reject()
 def test_invented_home_replacement(self):r=self.clear();r['after']['request']=r['after']['home'];self.reject()
 def test_missing_follow_inverse(self):r=self.clear('follow');r['after']['targets']=copy.deepcopy(r['before']['targets']);self.reject()
 def test_changed_reissue_order(self):
  rows=self.rows('follow');indices=[i for i,r in enumerate(rows)if r.get('event')=='reissue'and r.get('counter')==1757];self.assertGreater(len(indices),1);a,b=indices[:2];rows[a],rows[b]=rows[b],rows[a];self.reject()
 def test_arbitrary_embedded_pointer(self):self.clear()['before']['current'][1]^=4;self.reject()
 def test_missing_engine_gate(self):
  self.spec['engine_tests']=[]
  with self.assertRaises(ValueError):v.validate(self.spec)
if __name__=='__main__':unittest.main()
