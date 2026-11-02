"""Reject changed retail target ranking, delayed comparison or incomplete controls."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_ranking264 as v
class RankingTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  b=copy.deepcopy(self.bundle);edit(b)
  with self.assertRaises(ValueError):v.runtime(b,self.spec)
 def test_complete_original_contract(self):
  self.assertEqual(v.runtime(self.bundle,v.validate(self.spec)),dict(captures=24,public_markers=120,native_comparisons=18,domains=8))
 def test_missing_domain(self):self.reject(lambda b:b.pop('disarmed'))
 def test_missing_repeat(self):self.reject(lambda b:b['near'].pop(0))
 def test_missing_completion(self):self.reject(lambda b:b['near'][0]['rows'].pop())
 def test_wrong_game(self):self.reject(lambda b:b['near'][0]['rows'][0].update(sha256='0'*64))
 def test_owner_environment(self):self.reject(lambda b:b['near'][0]['rows'][0].update(env='A'))
 def test_instrumented_control(self):self.reject(lambda b:b['near'][2]['rows'].insert(1,dict(event='module',seq=1)))
 def test_control_trajectory_changed(self):self.reject(lambda b:b['near'][2].update(preload=b['near'][2]['preload'].replace('x=512.000','x=513.000',1)))
 def test_native_priority_changed(self):
  self.reject(lambda b:next(r for r in b['sticky'][0]['rows']if r['event']=='rank-end').update(key=[0,0]))
 def test_native_decision_changed(self):
  self.reject(lambda b:next(r for r in b['sticky'][0]['rows']if r['event']=='compare-end').update(accepted=1))
 def test_distance_changed(self):
  self.reject(lambda b:next(r for r in b['tie'][0]['rows']if r['event']=='distance').update(word=0))
 def test_bound_changed(self):
  self.reject(lambda b:next(r for r in b['near'][0]['rows']if r['event']=='rank').update(bound=[1,0]))
 def test_prediction_mode_changed(self):
  self.reject(lambda b:next(r for r in b['near'][0]['rows']if r['event']=='range').update(mode=1))
 def test_exemption_omitted(self):
  self.reject(lambda b:b['near'][0]['rows'].pop(next(i for i,r in enumerate(b['near'][0]['rows'])if r['event']=='exempt')))
 def test_original_distance_fixture_changed(self):
  s=copy.deepcopy(self.spec);s['distance_rows'][0]['distance']^=1
  with self.assertRaises(ValueError):v.validate(s)
 def test_instruction_omission(self):
  s=copy.deepcopy(self.spec);s['instructions'].pop(next(iter(s['instructions'])))
  with self.assertRaises(ValueError):v.validate(s)
 def test_source_pin_change(self):
  s=copy.deepcopy(self.spec);s['pins'][v.SOURCES[0]]='0'*64
  with self.assertRaises(ValueError):v.validate(s)
if __name__=='__main__':unittest.main()
