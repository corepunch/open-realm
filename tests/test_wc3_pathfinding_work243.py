"""Reject conflation of prepared idle Shift and delayed queued reconstruction."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work243 as V
class Work243Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def test_frozen_contract(self):V.validate(self.spec);V.validate_runtime(self.bundle,self.spec)
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop())
 def test_incomplete_marker_capture(self):self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
 def test_missing_shift(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='player-input')['plan'].update(shift=False))
 def test_idle_alt_loses_policy(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='bind'and r['flags']==14).update(flags=0))
 def test_delayed_activation_inherits_alt(self):self.reject(lambda b:[r for r in b['captures'][0]['rows']if r.get('event')=='bind'][-1].update(flags=14))
 def test_delayed_activation_reuses_canonical(self):self.reject(lambda b:[r for r in b['captures'][0]['rows']if r.get('event')=='bind'][-1].update(request=next(r['request']for r in b['captures'][0]['rows']if r.get('event')=='retain')))
 def test_missing_ready_recipient(self):self.reject(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows']if r.get('event')=='ready')))
 def test_prepared_attachment_order_reversed(self):self.reject(lambda b:next(r for r in b['captures'][2]['rows']if r.get('event')=='bind'and len(r['members'])==2)['members'].reverse())
 def test_premature_primary_ready(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='publish-ready'and r['flags']==14)['slots'].__setitem__(0,123))
 def test_changed_admission(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='publish').update(unit='0x1'))
 def test_unbalanced_recursion(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='trace-end').update(depth=1))
if __name__=='__main__':unittest.main()
