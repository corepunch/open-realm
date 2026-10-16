"""Keep pause stages, callback payloads and the retained broad evidence exact."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_pause287 as v
class PauseStageTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.s=json.loads(v.SPEC.read_text());cls.b=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def rejected(self,edit):
  b=copy.deepcopy(self.b);edit(b)
  with self.assertRaises(ValueError):v.validate_runtime(b,self.s)
 def test_complete_pinned_contract(self):v.validate(self.s)
 def test_lost_repeat(self):self.rejected(lambda b:b['captures'].pop())
 def test_control_instrumented(self):self.rejected(lambda b:b['captures'][2]['rows'].insert(1,dict(seq=0,event='admit-enter')))
 def test_observer_mutation(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='trace-end').update(readOnly=False))
 def test_early_ability_disable(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='validate-point-exit').update(result=221))
 def test_missing_suspension_begin(self):self.rejected(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows']if r.get('event')=='suspend-begin-enter')))
 def test_missing_suspension_end(self):self.rejected(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows']if r.get('event')=='suspend-end-exit')))
 def test_early_physical_release(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='resume-exit')['state'].update(flags=0))
 def test_counter_order_changed(self):
  def edit(b):
   next(r for r in b['captures'][0]['rows']if r.get('event')=='transient-enter')['state']['abilities'][0]['disabled'][1]=0
  self.rejected(edit)
 def test_pause_point_payload_changed(self):self.rejected(lambda b:b['captures'][2].update(preload=b['captures'][2]['preload'].replace('label=player-point-851973 order=851973 paused=1 x=656.000 y=656.000 px=0.000','label=player-point-851973 order=851973 paused=1 x=656.000 y=656.000 px=656.000')))
 def test_resume_reissue_not_erased(self):
  self.assertTrue(any('scene=15 label=player-point-851986'in p for p in self.s['public']))
  self.assertTrue(any('scene=15 label=unit-point-851986 order=0'in p for p in self.s['public']))
 def test_modal_evidence_retained_outside_core_projection(self):
  self.assertTrue(any('scene=12 label=player-immediate-852056'in p for p in self.s['public']))
  self.assertEqual(set(self.s['projection']),set(map(str,v.SCENES)))
  self.assertTrue(all(not r[0].endswith('-852056')for rows in self.s['projection'].values()for r in rows))
 def test_header_exact_retail_projection(self):self.assertEqual((ROOT/'games/warcraft-3/game/tests/fixtures/retail_pause287.h').read_text(),v.header(self.s))
 def test_prior_rejection_contract_unchanged(self):
  import verify_wc3_pathing_pause286 as old
  self.assertEqual(old.validate_runtime(json.loads(gzip.decompress(old.BUNDLE.read_bytes())),json.loads(old.SPEC.read_text()))['native_events'],1110)
if __name__=='__main__':unittest.main()
