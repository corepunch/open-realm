"""Reject contamination of the independent Attack/Captain acquisition evidence."""
import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_captain358_acquire as v
class Captain358AcquireTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.original=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.SPEC.with_suffix('.json.gz').read_bytes()))
 def setUp(self):self.spec=copy.deepcopy(self.original);self.cases=copy.deepcopy(self.bundle)
 def reject(self):
  with self.assertRaises(ValueError):v.runtime(self.cases,self.spec)
 def rows(self,event):return [r for r in self.cases[0]['rows']if r.get('event')==event]
 def test_complete_repeats_control(self):self.assertEqual(v.runtime(self.cases,self.spec)['fresh_scan_boundaries'],2)
 def test_missing_rebase_engine_gate(self):
  self.spec['engine_tests'].pop()
  with self.assertRaises(ValueError):v.validate(self.spec)
 def test_missing_repeat(self):self.cases.pop(1);self.reject()
 def test_unowned_process(self):self.cases[0]['rows'][0]['owned']=False;self.reject()
 def test_foreign_environment(self):self.cases[0]['rows'][0]['env']='A';self.reject()
 def test_foreign_map(self):self.cases[0]['rows'][0]['source_sha256']['map']='0'*64;self.reject()
 def test_instrumented_control(self):self.cases[2]['rows'].insert(1,{'event':'acquire-timer'});self.reject()
 def test_partial_capture(self):self.cases[0]['rows'].pop();self.reject()
 def test_wrong_idle_period(self):self.rows('acquisition-delays')[0]['idle']=0x3f800000;self.reject()
 def test_wrong_active_period(self):self.rows('acquisition-delays')[0]['active']=0x3f000000;self.reject()
 def test_missing_fresh_producer(self):
  counter=self.spec['projection']['counter'];self.cases[0]['rows']=[r for r in self.cases[0]['rows']if not(r.get('event')=='acquire-publish'and r.get('counter')==counter)];self.reject()
 def test_damage_contact_substitution(self):
  for r in self.cases[0]['rows']:
   if r.get('name')=='combat-notify':r['caller']=0x493af7
  self.reject()
 def test_lost_home_reissue(self):
  counter=self.spec['projection']['counter']
  for r in self.rows('acquire-timer'):
   if r.get('counter')==counter:r['after']['state']=1
  self.reject()
 def test_reordered_native_callbacks(self):
  counter=self.spec['projection']['counter'];rows=self.cases[0]['rows'];idx=[i for i,r in enumerate(rows)if r.get('counter')==counter and r.get('name')in('go-home','combat-notify')];self.assertEqual(len(idx),2);rows[idx[0]],rows[idx[1]]=rows[idx[1]],rows[idx[0]];self.reject()
if __name__=='__main__':unittest.main()
