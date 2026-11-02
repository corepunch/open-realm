"""Reject contaminations of Captain source policy and signed-counter evidence."""
import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_captain358_engagement as v
class Captain358EngagementTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.original=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.SPEC.with_suffix('.json.gz').read_bytes()))
 def setUp(self):self.spec=copy.deepcopy(self.original);self.groups=copy.deepcopy(self.bundle)
 def reject(self):
  with self.assertRaises(ValueError):v.runtime(self.groups,self.spec)
 def test_complete_repeats_controls(self):self.assertEqual(v.runtime(self.groups,self.spec)['public_markers'],1111)
 def test_missing_scene(self):del self.groups['negative'];self.reject()
 def test_missing_repeat(self):self.groups['near'].pop(1);self.reject()
 def test_missing_footer(self):self.groups['near'][0]['rows'].pop();self.reject()
 def test_foreign_map(self):self.groups['near'][0]['rows'][0]['source_sha256']['map']='0'*64;self.reject()
 def test_unowned_process(self):self.groups['negative'][0]['rows'][0]['owned']=False;self.reject()
 def test_instrumented_control(self):self.groups['near'][2]['rows'].insert(1,{'event':'module'});self.reject()
 def test_clamped_negative_counter(self):
  self.spec['scenes']['negative']['contacts'][0]['before']['counts'][2]=0
  for c in self.groups['negative'][:2]:
   for r in c['rows']:
    if r.get('name')=='combat-notify':r['before']['counts'][2]=0
  self.reject()
 def test_replaced_retained_point(self):
  for c in self.groups['near'][:2]:
   for r in c['rows']:
    if r.get('name')=='combat-notify':r['after']['request'][0]+=32
  self.spec['scenes']['near']['contacts'][0]['after']['request'][0]+=32;self.reject()
 def test_policy1_substitution(self):
  for r in self.groups['near'][0]['rows']:
   if r.get('name')=='refresh-target'and r.get('counter')==1424:r['policy']=1
  self.reject()
 def test_missing_route_preparation(self):
  for r in self.groups['near'][0]['rows']:
   if r.get('name')=='point-request'and r.get('counter')==1424:r['prepare']=0
  self.reject()
 def test_missing_source_loss(self):
  self.groups['lost'][0]['rows']=[r for r in self.groups['lost'][0]['rows']if r.get('name')!='member-removed'];self.reject()
 def test_loss_drops_roster_member(self):
  for case in self.groups['lost'][:2]:
   for r in case['rows']:
    if r.get('name')=='member-removed':r['after']['counts'][1]=5
  self.spec['scenes']['lost']['losses'][0]['after']['counts'][1]=5;self.reject()
if __name__=='__main__':unittest.main()
