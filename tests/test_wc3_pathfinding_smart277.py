"""Retail expectations reject lost packets, wrong tasks and instrumented controls."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from verify_wc3_pathing_smart277 import SPEC,BUNDLE,validate_runtime
class Smart277Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(SPEC.read_text());cls.bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()))
 def test_original_repeats_and_unhooked_controls(self):
  self.assertEqual(validate_runtime(self.bundle,self.spec),dict(captures=4,controls=2,public_markers=1073,loss_cases=5,visible_cases=1))
 def test_task_cannot_use_a_replacement_point(self):
  b=copy.deepcopy(self.bundle)
  head=next(r for r in b['matrix'][0]['rows']if r['event']=='dispatch-head'and r.get('scene')==1 and r['order']['command']==851971)
  row=next(r for r in b['matrix'][0]['rows']if r['event']=='point-task'and r.get('scene')==1 and r['seq']>head['seq'])
  row['point'][0]^=1
  with self.assertRaisesRegex(ValueError,'retain point'):validate_runtime(b,self.spec)
 def test_queue_identity_must_survive(self):
  b=copy.deepcopy(self.bundle)
  row=next(r for r in b['remove'][0]['rows']if r['event']=='dispatch-head'and r['order']['command']==851971)
  row['order']['identity'][0]+=1
  with self.assertRaisesRegex(ValueError,'retained packet'):validate_runtime(b,self.spec)
 def test_control_cannot_contain_hooks(self):
  b=copy.deepcopy(self.bundle);b['matrix'][2]['rows'].insert(-1,dict(event='trace-end',seq=1))
  with self.assertRaisesRegex(ValueError,'hooked control'):validate_runtime(b,self.spec)
 def test_visible_target_cannot_turn_into_point_fallback(self):
  b=copy.deepcopy(self.bundle)
  row=next(r for r in b['matrix'][0]['rows']if r['event']=='target-task'and r.get('scene')==6)
  row['code']=0xd016b
  with self.assertRaisesRegex(ValueError,'visible Smart'):validate_runtime(b,self.spec)
 def test_failed_input_cannot_certify_behavior(self):
  b=copy.deepcopy(self.bundle);next(r for r in b['matrix'][0]['rows']if r['event']=='player-input')['rc']=1
  with self.assertRaisesRegex(ValueError,'Shift RMB'):validate_runtime(b,self.spec)
if __name__=='__main__':unittest.main()
