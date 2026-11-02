"""Keep acquisition guards attached to the immutable original public witnesses."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_guard270 as v
class AvailabilityGuardTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  bundle=copy.deepcopy(self.bundle);edit(bundle)
  with self.assertRaises(ValueError):v.verify_live(bundle,self.spec)
 def test_complete(self):self.assertEqual(v.verify_live(self.bundle,self.spec)['public_lanes'],10)
 def test_source_inventory(self):v.validate(self.spec)
 def test_missing_guard(self):self.reject(lambda b:b['observe-b1'].pop(158))
 def test_changed_word(self):
  def edit(b):
   row=next(r for r in b['observe-b1']if r.get('caller')=='49e2b7');row['range']^=1
  self.reject(edit)
 def test_committed_second_guard(self):
  def edit(b):
   row=next(r for r in b['observe-b1']if r.get('caller')=='49e2b7');row['mode']=0
  self.reject(edit)
 def test_incomplete_observer(self):self.reject(lambda b:next(r for r in b['observe-b1']if r['event']=='trace-end').update(depth=1))
 def test_control_disagrees(self):self.reject(lambda b:b['preloads'].update({'control-b1':b['preloads']['control-b1'].replace('x=512.000','x=513.000')}))
 def test_wrong_map(self):self.reject(lambda b:b['observe-b1'][0]['source_sha256'].update(map='0'*64))
 def test_zero_speed_keeps_move(self):
  self.assertTrue(self.spec['lanes'][3]['move']);self.assertTrue(self.spec['lanes'][3]['exempt'])
 def test_dual_slot_uses_selected_range(self):
  lane=self.spec['lanes'][4];self.assertEqual(lane['guards'][-1],['49e2b7',0x42b40000,1,0]);self.assertFalse(lane['exempt'])
if __name__=='__main__':unittest.main()
