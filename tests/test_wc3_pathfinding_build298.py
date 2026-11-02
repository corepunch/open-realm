"""Guard original Human construction's transient Repair activation and resume."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_build298 as v
class Build298Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit,semantic=False):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):(v.validate_semantics if semantic else v.validate)(s)
 def row(self,s,event,scene=0):return next(e for e in s['native']if e['event']==event and e['scene']==scene)
 def test_repeats_control_and_semantics(self):v.validate(self.spec)
 def test_source_substitution(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCES[0],'0'*64))
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_changed_bundle(self):self.reject(lambda s:s.__setitem__('bundle_sha256','0'*64))
 def test_missing_public_stage(self):self.reject(lambda s:s['public'].pop())
 def test_missing_native_boundary(self):self.reject(lambda s:s['native'].pop())
 def test_build_not_retired_synchronously(self):self.reject(lambda s:self.row(s,'after-build-task-retire')['unit'].__setitem__('count',0),True)
 def test_repair_inserted_after_build(self):self.reject(lambda s:self.row(s,'transient-enter').__setitem__('arg1',0),True)
 def test_build_absent_on_repair_activation(self):self.reject(lambda s:self.row(s,'dispatch-enter',6)['unit'].__setitem__('count',2),True)
 def test_pause_preserves_repair_identity(self):
  def change(s):
   rows=[e for e in s['native']if e['scene']==3 and e['event']=='dispatch-enter'and e['order']['command']==852024]
   rows[1]['order']['identity']=[1,2]
  self.reject(change,True)
 def test_stop_cancels_work(self):
  def change(s):
   i=next(i for i,x in enumerate(s['public'])if 'scene=7 label=sample'in x and 'tick=705 'in x)
   s['public'][i]=s['public'][i].replace('order=0','order=852024')
  self.reject(change,True)
if __name__=='__main__':unittest.main()
