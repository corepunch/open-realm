"""Reject replacing native selection and public retail expectations."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_captain292 as v
class Captain292Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_repeats_and_uninstrumented_controls(self):v.validate(self.spec)
 def test_changed_public_order(self):self.reject(lambda s:s['public']['worker'].__setitem__(0,'R292 changed'))
 def test_changed_observer_only_tail(self):self.reject(lambda s:s['observed']['worker'].pop())
 def test_source_substitution(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCES[0],'0'*64))
 def test_missing_selection(self):self.reject(lambda s:s['native']['combat'].pop())
 def test_wrong_worker_default(self):
  def edit(s):next(x for x in s['native']['worker']if x['event']=='location-init')['xy']=[0,0]
  self.reject(edit)
 def test_repeat_reselects(self):
  def edit(s):[x for x in s['native']['structure']if x['event']=='select-leave'][-1]['owner']['locations'][0]=1
  self.reject(edit)
 def test_bundle_substitution(self):self.reject(lambda s:s.__setitem__('bundle_sha256','0'*64))
 def test_map_substitution(self):self.reject(lambda s:s['maps'].__setitem__('combat','0'*64))
if __name__=='__main__':unittest.main()
