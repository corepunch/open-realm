"""Point/flying approach evidence must preserve concrete owning commands."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_spell268 as v
class SpellApproachTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(v.FIXTURE.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def replace(self,s,old,new):
  i=next(i for i,m in enumerate(s['markers'])if old in m);s['markers'][i]=s['markers'][i].replace(old,new)
 def test_complete(self):self.assertEqual(len(v.heads(v.validate(self.spec)['markers'])),494)
 def test_missing_scene(self):self.reject(lambda s:s['markers'].pop())
 def test_point_substituted_move(self):self.reject(lambda s:self.replace(s,'s=0 l=0 label=accepted order=852488','s=0 l=0 label=accepted order=851986'))
 def test_fly_substituted_move(self):self.reject(lambda s:self.replace(s,'s=2 l=0 label=accepted order=852092','s=2 l=0 label=accepted order=851986'))
 def test_point_retired_early(self):self.reject(lambda s:self.replace(s,'s=0 l=31 label=sample order=852488','s=0 l=31 label=sample order=0'))
 def test_fly_retired_early(self):self.reject(lambda s:self.replace(s,'s=2 l=20 label=sample order=852092','s=2 l=20 label=sample order=0'))
 def test_point_retained_after_completion(self):self.reject(lambda s:self.replace(s,'s=0 l=32 label=sample order=0','s=0 l=32 label=sample order=852488'))
 def test_fly_retained_after_completion(self):self.reject(lambda s:self.replace(s,'s=2 l=21 label=sample order=0','s=2 l=21 label=sample order=852092'))
 def test_stop_retains_spell(self):self.reject(lambda s:self.replace(s,'s=3 l=10 label=stopped order=0','s=3 l=10 label=stopped order=852092'))
 def test_wrong_original(self):self.reject(lambda s:s.update(game_sha256='0'*64))
 def test_changed_source(self):self.reject(lambda s:s['pins'].update({v.SOURCES[0]:'0'*64}))
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_missing_engine_case(self):self.reject(lambda s:s['engine_tests'].pop())
 def test_parser(self):self.assertEqual(v.heads(['O268 tick=1 s=2 l=0 label=accepted order=852092 x=288']),[[2,0,'accepted',852092]])
if __name__=='__main__':unittest.main()
