"""Completed observations must retain the concrete override and its inverse."""
import gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_group354 as v
class MagicLeashPriorityEvidenceTests(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(v.SPEC.read_text());self.cases=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  edit(self.cases)
  with self.assertRaises(ValueError):v.runtime(self.cases,self.spec)
 def test_complete_repeats_and_observer_free_control(self):
  result=v.runtime(v.validate(self.spec),self.spec)
  self.assertEqual(result,dict(captures=3,scenes=5,public_markers=37,native_events=547))
 def test_missing_repeat(self):self.reject(lambda c:c.pop(1))
 def test_unowned_capture(self):self.reject(lambda c:c[0]['rows'][0].update(owned=False))
 def test_instrumented_control(self):self.reject(lambda c:c[2]['rows'].insert(1,dict(event='predicate',seq=0)))
 def test_incomplete_public_boundary(self):self.reject(lambda c:c[0]['rows'][-1].update(complete=False))
 def test_unfinished_hook_stack(self):
  self.reject(lambda c:next(r for r in c[0]['rows']if r.get('event')=='trace-end').update(depth=1))
 def test_changed_native_priority_word(self):
  self.reject(lambda c:next(r for r in c[0]['rows']if r.get('event')=='rank-end')['key'].__setitem__(0,0))
 def test_removed_inverse(self):
  def edit(c):
   rows=c[0]['rows'];rows.remove(next(r for r in rows if r.get('scene')==1 and r.get('tick')==30 and r.get('event')=='rank-end'))
  self.reject(edit)
 def test_diagnostic_midfunction_values_cannot_become_an_expected_contract(self):
  for c in self.cases[:2]:
   for r in c['rows']:
    if r.get('event')=='rank-end':r['key'][1]=0x08e40c27
  self.spec['events']=v.projection(self.cases[0]['rows'])
  with self.assertRaises(ValueError):v.runtime(self.cases,self.spec)
 def test_stun_name_cannot_substitute_for_magic_leash(self):
  for c in self.cases[:2]:
   next(r for r in c['rows']if r.get('event')=='rank-end')['key'][0]|=0x00400000
  self.spec['events']=v.projection(self.cases[0]['rows'])
  with self.assertRaises(ValueError):v.runtime(self.cases,self.spec)
 def test_incomplete_guard_inventory(self):
  self.spec['instructions'].pop(next(iter(self.spec['instructions'])))
  with self.assertRaises(ValueError):v.validate(self.spec)
 def test_unknown_map_base(self):
  self.spec['base_sha256']='0'*64
  with self.assertRaises(ValueError):v.validate(self.spec)
 def test_changed_observer_source(self):
  self.spec['pins']['tools/frida/research/group354_observer.js']='0'*64
  with self.assertRaises(ValueError):v.validate(self.spec)
if __name__=='__main__':unittest.main()
