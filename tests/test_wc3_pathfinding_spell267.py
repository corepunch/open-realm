"""Public spell identity evidence must reject Move substitution and lost phases."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_spell267 as v
class SpellHeadTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(v.FIXTURE.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_complete(self):self.assertEqual(len(v.validate(self.spec)['heads']),366)
 def test_move_substitution(self):self.reject(lambda s:s['heads'][1].__setitem__(3,851986))
 def test_no_accepted_head(self):self.reject(lambda s:s['heads'][1].__setitem__(3,0))
 def test_historical_head_after_completion(self):self.reject(lambda s:s['heads'][38].__setitem__(3,852092))
 def test_wrong_moving_target_retirement(self):self.reject(lambda s:s['heads'][285].__setitem__(3,852092))
 def test_follow_not_spell(self):self.reject(lambda s:s['heads'][123].__setitem__(3,852092))
 def test_missing_phase(self):self.reject(lambda s:s['heads'].pop())
 def test_changed_native(self):self.reject(lambda s:s.update(game_sha256='0'*64))
 def test_changed_source(self):self.reject(lambda s:s['pins'].update({v.SOURCES[0]:'0'*64}))
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_changed_original_expectations(self):self.reject(lambda s:s.update(base_sha256='0'*64))
 def test_missing_engine_path(self):self.reject(lambda s:s['engine_tests'].pop())
 def test_marker_parser(self):self.assertEqual(v.heads(['S184 tick=20 s=0 l=0 label=accepted order=852092 x=288.0']),[[0,0,'accepted',852092]])
if __name__=='__main__':unittest.main()
