"""Reject altered singleton publication evidence rather than fitting retail to the engine."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work239 as V
class Work239Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject_spec(self,change):
  spec=copy.deepcopy(self.spec);change(spec)
  with self.assertRaises(ValueError):V.validate(spec)
 def reject_live(self,change):
  bundle=copy.deepcopy(self.bundle);change(bundle)
  with self.assertRaises(ValueError):V.validate_runtime(bundle,self.spec)
 def last_publish(self,b):return [r for r in b['captures'][2]['rows']if r.get('event')=='publish'][-1]
 def test_frozen_contract(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['history_transitions'],2)
 def test_wrong_source(self):self.reject_spec(lambda s:s['pins'].__setitem__(V.SOURCES[0],'0'*64))
 def test_missing_engine_producer(self):self.reject_spec(lambda s:s['engine_tests'].pop())
 def test_missing_native_body(self):self.reject_spec(lambda s:s['instructions'].pop('6f6b94e3'))
 def test_wrong_singleton_flags(self):self.reject_spec(lambda s:s['expected'].update(singleton_flags=0x18))
 def test_retained_old_history(self):self.reject_live(lambda b:self.last_publish(b).update(after=self.last_publish(b)['before']))
 def test_cleared_other_unit(self):self.reject_live(lambda b:self.last_publish(b).update(unit='0x1234'))
 def test_missing_publication(self):
  self.reject_live(lambda b:b['captures'][2].update(rows=[r for r in b['captures'][2]['rows']if r.get('event')!='publish']))
 def test_incomplete_observer(self):self.reject_live(lambda b:next(r for r in b['captures'][3]['rows']if r.get('event')=='trace-end').update(depth=1))
 def test_singleton_prepared_a_request(self):self.reject_live(lambda b:b['captures'][0]['rows'].insert(2,dict(event='retain')))
 def test_incomplete_public_timeline(self):self.reject_live(lambda b:b['captures'][2]['rows'][-1].update(markers=82))
if __name__=='__main__':unittest.main()
