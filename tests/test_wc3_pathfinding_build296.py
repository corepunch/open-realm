"""Reject altered original unit work-clock evidence and owner semantics."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_build296 as v
class Build296Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit,semantic=False):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):(v.validate_semantics if semantic else v.validate)(s)
 def test_original_repeats_and_control(self):v.validate(self.spec)
 def test_source_substitution(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCES[0],'0'*64))
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_changed_bundle(self):self.reject(lambda s:s.__setitem__('bundle_sha256','0'*64))
 def test_missing_public_stage(self):self.reject(lambda s:s['public'].pop())
 def test_changed_native_entry(self):self.reject(lambda s:s['native'].pop())
 def test_parent_pause_delays_work(self):
  def edit(s):
   i=next(i for i,x in enumerate(s['public'])if 'tick=327 scene=3 label=sample'in x)
   s['public'][i]=s['public'][i].replace('order=0','order=1967274294')
  self.reject(edit,True)
 def test_worker_pause_does_not_cancel(self):
  def edit(s):next(x for x in s['native']if x['scene']==2 and x['event']=='execute-enter'and x['request']['delay']==0x3f800000)['request']['flags']=0
  self.reject(edit,True)
 def test_structure_removal_cancels_work(self):
  def edit(s):next(x for x in s['native']if x['scene']==5 and x['event']=='execute-enter'and x['request']['delay']==0x3f800000)['request']['flags']=65536
  self.reject(edit,True)
 def test_deadline_dispatch_clock_changed(self):
  def edit(s):next(x for x in s['native']if x['scene']==0 and x['event']=='execute-enter')['request']['clock']['time']+=1
  self.reject(edit,True)
 def test_generic_wait_confused_with_construction(self):
  def edit(s):next(x for x in s['native']if x['scene']==0 and x['event']=='wait-enter')['caller']='0x6785b0'
  self.reject(edit,True)
 def test_canceled_wait_replaces_move(self):
  def edit(s):
   i=next(i for i,x in enumerate(s['public'])if 'tick=427 scene=4 label=sample'in x)
   s['public'][i]=s['public'][i].replace('order=851986','order=0')
  self.reject(edit,True)
if __name__=='__main__':unittest.main()
