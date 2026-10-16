"""Return-window fixtures must not certify unobserved deferred cancellation."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_removal304 as v
class Removal304Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject_spec(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def reject_capture(self,edit):
  b=v.validate(self.spec);edit(b)
  with self.assertRaises(ValueError):v.validate_runtime(b,self.spec)
 def test_complete_original_repeats_and_control(self):v.validate(self.spec)
 def test_missing_repeat(self):self.reject_capture(lambda b:b['captures'].pop(0))
 def test_unowned_process(self):self.reject_capture(lambda b:b['captures'][0]['rows'][0].__setitem__('owned',False))
 def test_failed_control(self):self.reject_capture(lambda b:b['captures'][2]['rows'][-1].__setitem__('complete',False))
 def test_instrumented_control(self):self.reject_capture(lambda b:b['captures'][2]['rows'].insert(1,{'event':'state','seq':0}))
 def test_source_pin(self):self.reject_spec(lambda s:s['pins'].__setitem__(v.SOURCES[1],'0'*64))
 def test_missing_instruction(self):self.reject_spec(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_missing_engine_scope(self):self.reject_spec(lambda s:s['engine_tests'].pop())
 def test_cannot_drop_deferred_disable(self):
  def edit(b):
   rows=b['captures'][0]['rows'];rows.remove(next(r for r in rows if r.get('event')=='unit-stop-enter'and r['caller']=='0x5fc18a'))
  self.reject_capture(edit)
 def test_order_depth_must_follow_callback(self):
  def edit(b):
   row=next(r for r in b['captures'][0]['rows']if r.get('event')=='unit-stop-enter'and r['caller']=='0x5fc18a')
   next(a for a in row['state']['abilities']if a['classGetter']=='0x5fcd40')['disabled'][1]=1
  self.reject_capture(edit)
 def test_retirement_cannot_precede_dead_flag(self):
  def edit(b):
   row=next(r for r in b['captures'][0]['rows']if r.get('event')=='unit-stop-enter'and r['caller']=='0x5fc18a')
   row['state']['flags']&=~0x100
  self.reject_capture(edit)
if __name__=='__main__':unittest.main()
