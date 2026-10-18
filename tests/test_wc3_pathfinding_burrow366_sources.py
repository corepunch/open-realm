import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import wc3_burrow366_sources as v
class Burrow366SourcesTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,change):
  bundle=copy.deepcopy(self.bundle);change(bundle['cases'])
  with self.assertRaises(ValueError):v.runtime(bundle,self.spec)
 def bridge(self,cases):
  return [r[k]['invisibilityScalar']for r in cases[0]['rows']for k in('before','after','state')if isinstance(r.get(k),dict)and 'invisibilityScalar'in r[k]]
 def test_complete(self):self.assertEqual(v.runtime(self.bundle,self.spec)['native_events'],251)
 def test_missing_repeat(self):self.reject(lambda x:x.pop(1))
 def test_instrumented_control(self):self.reject(lambda x:x[2]['rows'].insert(1,dict(event='marker',seq=1)))
 def test_failed_execution(self):self.reject(lambda x:x[0]['rows'].insert(1,dict(event='trace-failed')))
 def test_wrong_binary(self):self.reject(lambda x:x[0]['rows'][0].update(sha256='bad'))
 def test_wrong_map(self):self.reject(lambda x:x[0]['rows'][0]['source_sha256'].update(map='bad'))
 def test_missing_completion(self):self.reject(lambda x:x[0]['rows'].pop())
 def test_changed_control(self):self.reject(lambda x:x[2].update(preload=x[2]['preload'].replace('S366 ','S366 changed ',1)))
 def test_fresh_generation_mutation(self):self.reject(lambda x:self.bridge(x)[-1].__setitem__(3,0x12345678))
 def test_fresh_slot_mutation(self):self.reject(lambda x:self.bridge(x)[-1].__setitem__(2,0x12345678))
 def test_scalar_refcount_literal(self):self.reject(lambda x:self.bridge(x)[-1].__setitem__(1,9))
 def test_scalar_vtable_literal(self):self.reject(lambda x:self.bridge(x)[-1].__setitem__(0,0x12345678))
 def test_unexpected_listener_pointer(self):self.reject(lambda x:self.bridge(x)[-1].__setitem__(5,0x12345678))
 def test_unexpected_modifier_pointer(self):self.reject(lambda x:self.bridge(x)[-1].__setitem__(4,0x12345678))
 def availability(self,cases):
  return next(r['availability']for r in cases[0]['rows']if 'availability'in r)
 def test_changed_execution_counter(self):self.reject(lambda x:self.availability(x).__setitem__('execution',123))
 def test_changed_order_counter(self):self.reject(lambda x:self.availability(x).__setitem__('orders',123))
 def test_changed_literal_word50(self):self.reject(lambda x:self.availability(x).__setitem__('word50',0x12345678))
 def test_changed_class(self):self.reject(lambda x:self.availability(x).__setitem__('class','b67448'))
 def test_changed_held_flags(self):self.reject(lambda x:self.availability(x).__setitem__('flags',0))
if __name__=='__main__':unittest.main()
