"""Protect original movement and footprint expectations against accidental rebasing."""
import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_work244 as V
class Work244Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def test_complete_captures(self):V.validate_runtime(self.bundle,self.spec)
 def test_literal_headers_from_retail(self):
  motion,texture=V.headers(self.bundle,self.spec['category_kernel'])
  self.assertEqual(motion,V.MOTION.read_text());self.assertEqual(texture,V.TEXTURE.read_text())
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop())
 def test_incomplete_capture(self):self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
 def test_missing_shift(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='player-input')['plan'].update(shift=False))
 def test_changed_velocity(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='motion244')['after']['pose'].__setitem__(2,0))
 def test_changed_clock(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='motion244')['after']['clock'].__setitem__(0,0))
 def test_changed_texture_category(self):self.reject(lambda b:next(r for r in b['captures'][2]['rows']if r.get('event')=='texture244')['categories'].__setitem__(4,24))
 def test_changed_hierarchy(self):self.reject(lambda b:next(r for r in b['captures'][2]['rows']if r.get('event')=='hierarchy244')['maps'][0]['words'].__setitem__(0,123))
 def test_unbalanced_observer(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='trace-end').update(depth=1))
if __name__=='__main__':unittest.main()
