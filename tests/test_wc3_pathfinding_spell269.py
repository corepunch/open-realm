"""Frozen point-spell evidence must remain attached to its original producer."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_spell269 as v
class PointSpellTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(v.FIXTURE.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_complete(self):self.assertEqual(len(v.validate(self.spec)['motion']),52)
 def test_missing_motion(self):self.reject(lambda s:s['motion'].pop())
 def test_duplicate_owner_visit(self):self.reject(lambda s:s['motion'].insert(1,s['motion'][0]))
 def test_wrong_counter(self):self.reject(lambda s:s['motion'][1].__setitem__(0,1091))
 def test_collision_added_to_physical_range(self):self.reject(lambda s:s['motion'][0].__setitem__(8,0x41d00000))
 def test_changed_numerical_word(self):self.reject(lambda s:s['motion'][10].__setitem__(4,s['motion'][10][4]^1))
 def test_changed_clock(self):self.reject(lambda s:s['motion'][10].__setitem__(1,s['motion'][10][1]^1))
 def test_wrong_original(self):self.reject(lambda s:s.update(game_sha256='0'*64))
 def test_source_changed(self):self.reject(lambda s:s['pins'].update({v.SOURCES[0]:'0'*64}))
 def test_missing_source(self):self.reject(lambda s:s['pins'].pop(v.HEADER))
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_missing_engine_case(self):self.reject(lambda s:s['engine_tests'].pop())
 def test_retired_owner_row_is_not_replayed(self):
  member=dict(id=[1,2],grp=[8,9],pos=[3,4],vel=[5,6],range=7)
  rows=[dict(event='marker',value='O268 s=0 label=begin-setup'),
   dict(event='begin-point',c=1090,self=[1,2],point=[1,2],range=0x44480000,arrival=1,cantpath=2,flag=1),
   dict(event='spell-owner-clock',phase='begin',c=1091,g='live',clock=[10,0,11]),
   dict(event='gtick',c=1091,g='live',id=[8,9],flags=0,age=0,comp=0,cd=0,unseen=0,path=None,members=[member]),
   dict(event='spell-owner-clock',phase='begin',c=1091,g='retired',clock=[10,0,11]),
   dict(event='gtick',c=1091,g='retired',id=[12,13],flags=0,age=0,comp=0,cd=0,unseen=0,path=None,members=[member])]
  out=v.extract(rows);self.assertEqual(out['motion'],[[1091,10,0,11,3,4,5,6,7]]);self.assertEqual(len(out['visits']),2)
if __name__=='__main__':unittest.main()
