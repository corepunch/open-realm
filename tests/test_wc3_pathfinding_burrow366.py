import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import wc3_burrow366 as v
class Burrow366Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,change):
  bundle=copy.deepcopy(self.bundle);change(bundle['cases'])
  with self.assertRaises(ValueError):v.runtime(bundle,self.spec)
 def bridge(self,cases):
  return [r[k]['invisibilityScalar']for r in cases[0]['rows']for k in('before','after','state')if isinstance(r.get(k),dict)and 'invisibilityScalar'in r[k]]
 def test_complete(self):self.assertEqual(v.runtime(self.bundle,self.spec)['native_events'],1042)
 def test_missing_repeat(self):self.reject(lambda x:x.pop(1))
 def test_instrumented_control(self):self.reject(lambda x:x[2]['rows'].insert(1,dict(event='marker',seq=1)))
 def test_failed_execution(self):self.reject(lambda x:x[0]['rows'].insert(1,dict(event='trace-failed')))
 def test_wrong_binary(self):self.reject(lambda x:x[0]['rows'][0].update(sha256='bad'))
 def test_wrong_map(self):self.reject(lambda x:x[0]['rows'][0]['source_sha256'].update(map='bad'))
 def test_missing_completion(self):self.reject(lambda x:x[0]['rows'].pop())
 def test_changed_control(self):self.reject(lambda x:x[2].update(preload=x[2]['preload'].replace('B366 ','B366 changed ',1)))
 def test_fresh_generation_mutation(self):self.reject(lambda x:self.bridge(x)[-1].__setitem__(3,0x12345678))
 def test_fresh_slot_mutation(self):self.reject(lambda x:self.bridge(x)[-1].__setitem__(2,0x12345678))
 def test_scalar_refcount_literal(self):self.reject(lambda x:self.bridge(x)[-1].__setitem__(1,9))
 def test_scalar_vtable_literal(self):self.reject(lambda x:self.bridge(x)[-1].__setitem__(0,0x12345678))
 def test_listener_reuse_mutation(self):
  def mutate(x):
   words=[w for w in self.bridge(x)if w[5]];self.assertGreater(len(words),1);words[-1][5]=0x12345678
  self.reject(mutate)
 def test_modifier_alias_mutation(self):
  def mutate(x):
   words=[w for w in self.bridge(x)if w[4]and w[5]and w[4]!=w[5]];self.assertTrue(words);words[-1][5]=words[-1][4]
  self.reject(mutate)
 def test_listener_null_mutation(self):
  def mutate(x):next(w for w in self.bridge(x)if w[5])[5]=0
  self.reject(mutate)
 def test_consistent_heap_relocation_allowed(self):
  bundle=copy.deepcopy(self.bundle)
  for case in bundle['cases'][:2]:
   for row in case['rows']:
    for key in('before','after','state'):
     if isinstance(row.get(key),dict)and 'invisibilityScalar'in row[key]:
      words=row[key]['invisibilityScalar']
      for i in(4,5):
       if words[i]:words[i]+=0x100000
  self.assertEqual(v.runtime(bundle,self.spec)['native_events'],1042)
if __name__=='__main__':unittest.main()
