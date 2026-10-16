"""Keep forced-position chronology and literal retail expectations immutable."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_stop285 as v

class ForcedPositionChronologyTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.s=json.loads(v.SPEC.read_text());cls.b=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def rejected(self,mutate):
  b=copy.deepcopy(self.b);mutate(b)
  with self.assertRaises(ValueError):v.validate_runtime(b,self.s)
 def test_complete_pinned_contract(self):v.validate(self.s)
 def test_lost_repeat(self):self.rejected(lambda b:b['captures'].pop())
 def test_control_instrumented(self):self.rejected(lambda b:b['captures'][2]['rows'].insert(1,dict(seq=0,event='unit-stop-enter')))
 def test_observer_not_read_only(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='trace-end').update(readOnly=False))
 def test_missing_loc_wrapper(self):self.rejected(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows']if r.get('event')=='set-position-loc-enter')))
 def test_changed_native_scalar(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='position-admission-enter').update(xy=[0,0]))
 def test_placement_reordered(self):
  def edit(b):
   rows=b['captures'][0]['rows'];p=next(i for i,r in enumerate(rows)if r.get('event')=='position-admission-enter');rows[p],rows[p-1]=rows[p-1],rows[p]
  self.rejected(edit)
 def test_forced_stop_caller_changed(self):self.rejected(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='forced-stop-enter').update(caller='0x215620'))
 def test_early_stop_in_suspended_scene(self):
  def edit(b):
   next(r for r in b['captures'][0]['rows']if r.get('event')=='set-position-enter'and r.get('scene')==32).update(event='unit-stop-enter')
  self.rejected(edit)
 def test_control_public_head_changed(self):self.rejected(lambda b:b['captures'][2].update(preload=b['captures'][2]['preload'].replace('order=851973','order=0')))
 def test_missing_delayed_callback(self):self.rejected(lambda b:b['captures'][2].update(preload=b['captures'][2]['preload'].replace('label=player-enter','label=lost')))
 def test_header_is_exact_retail_export(self):self.assertEqual((ROOT/'games/warcraft-3/game/tests/fixtures/retail_stop285.h').read_text(),v.header(self.s))
 def test_unmodified_prior_retail_contract(self):
  import verify_wc3_pathing_stop284 as prior
  self.assertEqual(prior.validate_runtime(json.loads(gzip.decompress(prior.BUNDLE.read_bytes())),json.loads(prior.SPEC.read_text()))['native_events'],2436)

if __name__=='__main__':unittest.main()
