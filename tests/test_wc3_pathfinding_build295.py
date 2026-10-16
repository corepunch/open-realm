"""Reject altered retail construction callback evidence."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_build295 as v
class Build295Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_original_repeats_and_control(self):v.validate(self.spec)
 def test_source_substitution(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCES[0],'0'*64))
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_changed_bundle(self):self.reject(lambda s:s.__setitem__('bundle_sha256','0'*64))
 def test_missing_public_stage(self):self.reject(lambda s:s['public'].pop())
 def test_premature_human_repair(self):
  def edit(s):
   i=next(i for i,x in enumerate(s['public'])if 'scene=0 label=construct-start'in x)
   s['public'][i]=s['public'][i].replace('order=1749170485','order=852024')
  self.reject(edit)
 def test_premature_health_initialization(self):
  def edit(s):
   i=next(i for i,x in enumerate(s['public'])if 'scene=0 label=construct-start'in x)
   s['public'][i]=s['public'][i].replace('buildlife=500.000','buildlife=50.000')
  self.reject(edit)
 def test_changed_work_depth(self):
  def edit(s):next(x for x in s['native']if x['event']=='inside-work-enter')['unit']['depth']=2
  self.reject(edit)
 def test_changed_native_entry(self):self.reject(lambda s:s['native'].pop())
if __name__=='__main__':unittest.main()
