"""Reject incomplete or contaminated Captain combat-contact evidence."""
import copy,gzip,importlib.util,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_captain358 as verify
class Captain358EvidenceTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(verify.SPEC.read_text())
  cls.cases=json.loads(gzip.decompress(verify.SPEC.with_suffix('.json.gz').read_bytes()))
 def setUp(self):self.spec=copy.deepcopy(type(self).spec);self.cases=copy.deepcopy(type(self).cases)
 def reject(self):
  with self.assertRaises(ValueError):verify.runtime(self.cases,self.spec)
 def test_complete_public_repeats_and_control(self):self.assertEqual(verify.runtime(self.cases,self.spec)['contact_boundaries'],2)
 def test_missing_control(self):self.cases.pop();self.reject()
 def test_unowned_process(self):self.cases[0]['rows'][0]['owned']=False;self.reject()
 def test_instrumented_control(self):self.cases[2]['rows'].insert(1,{'event':'module'});self.reject()
 def test_foreign_map(self):self.cases[1]['rows'][0]['source_sha256']['map']='0'*64;self.reject()
 def test_missing_completion(self):self.cases[0]['rows'].pop();self.reject()
 def test_different_public_positions(self):self.cases[1]['preload']=self.cases[1]['preload'].replace('100.000','101.000',1);self.reject()
 def test_missing_contact(self):self.cases[0]['rows']=[r for r in self.cases[0]['rows']if not(r.get('event')=='captain-call'and r.get('name')=='combat-notify')];self.reject()
 def test_invented_retreat(self):
  for case in self.cases[:2]:
   for r in case['rows']:
    if r.get('event')=='captain-call'and r.get('name')=='combat-notify':r['after']['flags']|=2
  self.spec['contact'][0]['after']['flags']|=2;self.reject()
 def test_invented_strength_counter(self):
  for case in self.cases[:2]:
   for r in case['rows']:
    if r.get('event')=='captain-call'and r.get('name')=='combat-notify':r['before']['counts'][2]=6
  self.spec['contact'][0]['before']['counts'][2]=6;self.reject()
 def test_missing_go_home_publication(self):
  self.cases[0]['rows']=[r for r in self.cases[0]['rows']if not(r.get('event')=='captain-call'and r.get('name')=='go-home')];self.reject()
 def test_wrong_request_radius(self):
  for r in self.cases[0]['rows']:
   if r.get('event')=='captain-call'and r.get('name')=='point-request':r['range']=0
  self.reject()
if __name__=='__main__':unittest.main()
