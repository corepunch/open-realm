"""Freeze-family evidence must preserve distinct counted owners and order inverses."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_group359 as v
class FreezeEvidenceTests(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(v.SPEC.read_text());self.cases=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  edit(self.cases)
  with self.assertRaises(ValueError):v.runtime(self.cases,self.spec)
 def test_complete_counter_and_order_repeats(self):
  self.assertEqual(v.runtime(v.validate(self.spec),self.spec),dict(captures=9,scenes=16,public_markers=162,native_events=2209))
 def test_missing_experiment(self):self.reject(lambda c:c.pop())
 def test_missing_repeat(self):self.reject(lambda c:c[0]['cases'].pop(1))
 def test_unowned_capture(self):self.reject(lambda c:c[0]['cases'][0]['rows'][0].update(owned=False))
 def test_instrumented_control(self):self.reject(lambda c:c[1]['cases'][2]['rows'].insert(1,dict(event='predicate',seq=0)))
 def test_incomplete_public_boundary(self):self.reject(lambda c:c[0]['cases'][0]['rows'][-1].update(complete=False))
 def test_unfinished_hook_stack(self):self.reject(lambda c:next(r for r in c[1]['cases'][0]['rows']if r.get('event')=='trace-end').update(depth=1))
 def test_changed_priority_word(self):self.reject(lambda c:next(r for r in c[0]['cases'][0]['rows']if r.get('event')=='rank-end')['key'].__setitem__(0,0))
 def test_removed_inverse(self):
  def edit(c):
   rows=c[0]['cases'][0]['rows'];rows.remove(next(r for r in rows if r.get('event')=='stun-remove-end'))
  self.reject(edit)
 def changed_semantics(self,edit):
  for case in self.cases[0]['cases'][:2]:edit(case['rows'])
  self.spec['experiments'][0]['events']=v.projection(self.cases[0]['cases'][0]['rows'])
  with self.assertRaises(ValueError):v.runtime(self.cases,self.spec)
 def test_freeze_cannot_use_execution_only_counter(self):
  self.changed_semantics(lambda rows:next(r for r in rows if r.get('event')=='stun-initialize')['parameters'].__setitem__(0,0))
 def test_breath_cannot_disable_new_orders(self):
  self.changed_semantics(lambda rows:next(r for r in rows if r.get('event')=='stun-initialize-end'and r.get('scene')==4)['after'].update(orders=1))
 def test_inverse_cannot_leak_execution_depth(self):
  self.changed_semantics(lambda rows:next(r for r in rows if r.get('event')=='stun-remove-end')['after'].update(execution=1))
 def test_lethal_inverse_cannot_be_delayed_to_a_later_tick(self):
  for case in self.cases[2]['cases'][:2]:
   next(r for r in case['rows']if r.get('event')=='stun-remove')['tick']+=1
  self.spec['experiments'][2]['events']=v.projection(self.cases[2]['cases'][0]['rows'])
  with self.assertRaises(ValueError):v.runtime(self.cases,self.spec)
 def test_unknown_map_base(self):
  self.spec['experiments'][0]['base_sha256']='0'*64
  with self.assertRaises(ValueError):v.validate(self.spec)
 def test_incomplete_instruction_inventory(self):
  self.spec['instructions'].pop(next(iter(self.spec['instructions'])))
  with self.assertRaises(ValueError):v.validate(self.spec)
 def test_incomplete_class_inventory(self):
  self.spec['classes'].pop()
  with self.assertRaises(ValueError):v.validate(self.spec)
 def test_changed_observer(self):
  self.spec['pins']['tools/frida/research/group359_observer.js']='0'*64
  with self.assertRaises(ValueError):v.validate(self.spec)
if __name__=='__main__':unittest.main()
