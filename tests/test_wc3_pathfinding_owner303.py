"""Reject incomplete evidence and invented Move attachment/recovery chronology."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_owner303 as v
class Owner303Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject_spec(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def reject_capture(self,edit):
  b=v.validate(self.spec);edit(b)
  with self.assertRaises(ValueError):v.validate_runtime(b,self.spec)
 def test_complete_original_repeats_and_control(self):v.validate(self.spec)
 def test_missing_repeat(self):self.reject_capture(lambda b:b['captures'].pop(0))
 def test_missing_public_marker(self):self.reject_capture(lambda b:b['captures'][0]['rows'].pop(2))
 def test_unowned_process(self):self.reject_capture(lambda b:b['captures'][0]['rows'][0].__setitem__('owned',False))
 def test_failed_control(self):self.reject_capture(lambda b:b['captures'][2]['rows'][-1].__setitem__('complete',False))
 def test_instrumented_control(self):self.reject_capture(lambda b:b['captures'][2]['rows'].insert(1,{'event':'state','seq':0}))
 def test_source_pin(self):self.reject_spec(lambda s:s['pins'].__setitem__(v.SOURCES[1],'0'*64))
 def test_missing_instruction(self):self.reject_spec(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_missing_engine_scope(self):self.reject_spec(lambda s:s['engine_tests'].pop())
 def test_absent_move_must_stay_absent(self):
  def edit(b):
   row=next(r for r in b['captures'][0]['rows']if r.get('scene')==1 and r.get('event')=='death-enter')
   row['state']['abilities'][0]['classGetter']='0x5fcd40'
  self.reject_capture(edit)
 def test_third_recovery_has_counted_owner(self):
  def edit(b):
   row=next(r for r in b['captures'][0]['rows']if r.get('event')=='unit-stop-enter' and r['caller']=='0x5fc18a')
   row['caller']='0x600433'
  self.reject_capture(edit)
 def test_return_window_cannot_drop_deferred_owner(self):
  def edit(b):
   r=b['captures'][0]['rows'];r.remove(next(x for x in r if x.get('scene')==4 and x.get('event')=='unit-stop-enter' and x['caller']=='0x5fc18a'))
  self.reject_capture(edit)
if __name__=='__main__':unittest.main()
