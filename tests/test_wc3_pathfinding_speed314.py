"""Reject damaged exceptional-speed producer, control and motion evidence."""
import gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_speed314 as v
class Speed314Evidence(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(v.SPEC.read_text());self.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  edit()
  with self.assertRaises(ValueError):v.validate(self.spec,self.bundle)
 def event(self,name):return next(r for r in self.bundle['foot'][0]['rows']if r['event']==name)
 def test_complete(self):self.assertEqual(v.validate(self.spec,self.bundle),dict(captures=15,native_events=76012,public_markers=3855,states=4005,commits=2805))
 def test_missing_family(self):self.reject(lambda:self.bundle.pop('float'))
 def test_missing_repeat(self):self.reject(lambda:self.bundle['foot'].pop(1))
 def test_missing_control(self):self.reject(lambda:self.bundle['foot'].pop())
 def test_missing_native_event(self):self.reject(lambda:self.bundle['foot'][0]['rows'].remove(self.event('commit-leave')))
 def test_changed_producer(self):self.reject(lambda:self.event('public-default-height').update(value=0))
 def test_changed_setter(self):self.reject(lambda:self.event('setter-enter').update(value=0))
 def test_changed_public_speed(self):self.reject(lambda:self.event('public-speed').update(value=0))
 def test_changed_clock(self):self.reject(lambda:self.event('commit-leave').update(c=0))
 def test_engine_words_cannot_replace_retail(self):self.reject(lambda:self.spec['cases']['foot']['states'][2][3].__setitem__(8,0))
 def test_missing_instruction(self):self.reject(lambda:self.spec['instructions'].pop(next(iter(self.spec['instructions']))))
 def test_hooked_control(self):self.reject(lambda:self.bundle['foot'][2]['rows'].insert(1,dict(event='trace-end')))
 def test_control_public_differs(self):self.reject(lambda:self.bundle['foot'][2].update(preload=''))
if __name__=='__main__':unittest.main()
