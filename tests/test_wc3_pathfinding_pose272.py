"""Reject incomplete or altered original support and published-pose evidence."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_pose272 as v
class Slope272Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  b=copy.deepcopy(self.bundle);edit(b)
  with self.assertRaises(ValueError):v.verify_live(b,self.spec)
 def row(self,b,event):return next(r for r in b['captures'][0]['rows']if r['event']==event)
 def test_complete(self):self.assertEqual(v.verify_live(self.bundle,self.spec)[0]['poses'],920)
 def test_source_inventory(self):v.validate(self.spec)
 def test_native_matrix_word(self):self.reject(lambda b:self.row(b,'pose')['matrix'].__setitem__(0,0))
 def test_support_not_published(self):self.reject(lambda b:self.row(b,'support')['point'].__setitem__(2,1))
 def test_missing_tap(self):self.reject(lambda b:self.row(b,'pose')['samples'].pop())
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop(1))
 def test_unowned_capture(self):self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
 def test_control_must_be_unhooked(self):self.reject(lambda b:b['captures'][2]['rows'].append(dict(event='trace-end',seq=1)))
 def test_observer_completion(self):self.reject(lambda b:self.row(b,'trace-end').update(depth=1))
 def test_changed_observer(self):self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].update({'pose272_observer.js':'0'*64}))
 def test_changed_map(self):self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].update(map='0'*64))
 def test_missing_grid(self):self.reject(lambda b:b['captures'][0]['rows'].remove(self.row(b,'grid')))
if __name__=='__main__':unittest.main()
