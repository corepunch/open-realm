"""Reject incomplete or reordered native Captain query evidence."""
import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_captain369_selection as v
class Captain369SelectionTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.original=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.SPEC.with_suffix('.json.gz').read_bytes()))
 def setUp(self):self.spec=copy.deepcopy(self.original);self.groups=copy.deepcopy(self.bundle)
 def rows(self,scene='clear'):return self.groups[scene][0]['rows']
 def reject(self):
  with self.assertRaises(ValueError):v.runtime(self.groups,self.spec)
 def test_completed_repeats_controls(self):self.assertEqual(v.runtime(self.groups,self.spec)['public_markers'],1533)
 def test_missing_repeat(self):self.groups['transfer'].pop();self.reject()
 def test_partial_capture(self):self.rows().pop();self.reject()
 def test_unowned_process(self):self.rows()[0]['owned']=False;self.reject()
 def test_foreign_environment(self):self.rows()[0]['env']='A';self.reject()
 def test_wrong_map(self):self.rows()[0]['source_sha256']['map']='0'*64;self.reject()
 def test_instrumented_control(self):self.groups['clear'][2]['rows'].insert(1,{'event':'captain-call'});self.reject()
 def test_changed_period(self):next(r for r in self.rows()if r.get('name')=='begin-selection')['after']['selection']['request']['period']^=1;self.reject()
 def test_changed_periodic_owner(self):next(r for r in self.rows()if r.get('name')=='begin-selection')['after']['periodic']['flags']^=1;self.reject()
 def test_reordered_candidates(self):
  rows=self.rows('candidates');indices=[i for i,r in enumerate(rows)if r.get('event')=='selection-candidate'];a,b=indices[:2];rows[a],rows[b]=rows[b],rows[a];self.reject()
 def test_wrong_random_stream(self):next(r for r in self.rows('candidates')if r.get('event')=='selection-random')['stream']=12;self.reject()
 def test_missing_candidate_tick(self):
  rows=self.rows('candidates');rows.pop(next(i for i,r in enumerate(rows)if r.get('name')=='selection-poll'));self.reject()
 def test_changed_publication_state(self):next(r for r in self.rows('reachable')if r.get('name')=='selection-poll')['after']['state']=2;self.reject()
 def test_arbitrary_embedded_pointer(self):next(r for r in self.rows()if r.get('name')=='begin-selection')['before']['current'][1]^=4;self.reject()
 def test_missing_primary_loss_rearm(self):
  rows=self.rows('loss');indices=[i for i,r in enumerate(rows)if r.get('name')=='begin-selection'];rows.pop(indices[1]);self.reject()
 def test_wrong_primary_loss_caller(self):
  rows=self.rows('loss');begin=[r for r in rows if r.get('name')=='begin-selection'];begin[1]['caller']^=1;self.reject()
 def test_missing_engine_gate(self):
  self.spec['engine_tests']=[]
  with self.assertRaises(ValueError):v.validate(self.spec)
if __name__=='__main__':unittest.main()
