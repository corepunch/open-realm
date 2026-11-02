"""Reject altered paused-admission traces and preserve literal retail fixtures."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_pause286 as v

class PausedAdmissionTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.s=json.loads(v.SPEC.read_text());cls.b=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def rejected(self,mutate):
  b=copy.deepcopy(self.b);mutate(b)
  with self.assertRaises(ValueError):v.validate_runtime(b,self.s)
 def test_complete_pinned_contract(self):v.validate(self.s)
 def test_lost_repeat(self):self.rejected(lambda b:b['captures'].pop())
 def test_control_instrumented(self):self.rejected(lambda b:b['captures'][2]['rows'].insert(1,dict(seq=0,event='admit-enter')))
 def test_observer_not_read_only(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='trace-end').update(readOnly=False))
 def test_wrong_target_validator_result(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='validate-target-exit').update(result=0))
 def test_disabled_counter_removed(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='validate-point-enter')['state'].update(ability_disable=0))
 def test_missing_target_validator(self):self.rejected(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows']if r.get('event')=='validate-target-enter')))
 def test_early_stop_event(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='append-enter').update(event='immediate-event-enter'))
 def test_successors_not_cancelled(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='cancel-successors-exit')['state'].update(count=2))
 def test_identity_relationship_changed(self):
  def edit(b):
   next(r for r in b['captures'][0]['rows']if r.get('event')=='append-exit')['state']['head']=[123,456]
  self.rejected(edit)
 def test_control_public_acceptance_changed(self):self.rejected(lambda b:b['captures'][2].update(preload=b['captures'][2]['preload'].replace('label=first-false','label=first-true')))
 def test_pause_callbacks_retained(self):
  self.assertTrue(any('label=issued-851973'in p for p in self.s['public']))
  self.assertTrue(any('label=issued-852056'in p for p in self.s['public']))
 def test_header_is_exact_retail_export(self):self.assertEqual((ROOT/'games/warcraft-3/game/tests/fixtures/retail_pause286.h').read_text(),v.header(self.s))
 def test_prior_forced_position_contract_unchanged(self):
  import verify_wc3_pathing_stop285 as prior
  self.assertEqual(prior.validate_runtime(json.loads(gzip.decompress(prior.BUNDLE.read_bytes())),json.loads(prior.SPEC.read_text()))['native_events'],5240)

if __name__=='__main__':unittest.main()
