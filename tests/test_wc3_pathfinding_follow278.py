"""Structure evidence rejects persistence, range and observer/control corruption."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from verify_wc3_pathing_follow278 import SPEC,BUNDLE,validate_runtime
class Follow278Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(SPEC.read_text());cls.bundle=json.loads(gzip.decompress(BUNDLE.read_bytes()))
 def test_original_repeats_and_unhooked_control(self):
  self.assertEqual(validate_runtime(self.bundle,self.spec),dict(captures=2,controls=1,public_markers=797,task_cases=6))
 def altered(self,scene):
  b=copy.deepcopy(self.bundle)
  return b,next(r for r in b['captures'][0]['rows']if r['event']=='target-task'and r.get('scene')==scene)
 def test_static_structure_cannot_use_persistent_follow(self):
  b,r=self.altered(1);r['code']=0xd0173
  with self.assertRaisesRegex(ValueError,'task/range'):validate_runtime(b,self.spec)
 def test_zero_range_cannot_turn_into_structure_margin(self):
  b,r=self.altered(2);r['range']=0x42c80000
  with self.assertRaisesRegex(ValueError,'task/range'):validate_runtime(b,self.spec)
 def test_rooted_ancient_retains_move_ability(self):
  b,r=self.altered(4);r['targetState']['move']='0x0'
  with self.assertRaisesRegex(ValueError,'classification'):validate_runtime(b,self.spec)
 def test_uprooted_ancient_cannot_use_one_shot(self):
  b,r=self.altered(5);r['code']=0xd0174
  with self.assertRaisesRegex(ValueError,'task/range'):validate_runtime(b,self.spec)
 def test_control_cannot_have_native_hooks(self):
  b=copy.deepcopy(self.bundle);b['captures'][2]['rows'].insert(-1,dict(event='trace-end',seq=1))
  with self.assertRaisesRegex(ValueError,'hooked control'):validate_runtime(b,self.spec)
 def test_public_completion_cannot_be_changed(self):
  b=copy.deepcopy(self.bundle);r=next(r for r in b['captures'][0]['rows']if r['event']=='marker'and r.get('scene')==1 and 'label=settled'in r['value']);r['value']=r['value'].replace('order=0','order=851986')
  with self.assertRaisesRegex(ValueError,'public completion'):validate_runtime(b,self.spec)
if __name__=='__main__':unittest.main()
