"""Keep native Alt request classes, readiness and physical order frozen."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work238 as V
class Work238Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(V.FIXTURE.read_text())
 def reject(self,change):
  s=copy.deepcopy(self.spec);change(s)
  with self.assertRaises(ValueError):V.validate(s)
 def split(self,s):return next(r for r in s['kernel']['rows']if r['flight']==6 and not r['grounded'])
 def test_frozen_native_domains(self):V.validate(self.spec)
 def test_missing_original_case(self):self.reject(lambda s:s['kernel']['rows'].pop())
 def test_premature_publication(self):self.reject(lambda s:self.split(s)['births'][0].update(after=1))
 def test_context_order_instead_of_birth_order(self):self.reject(lambda s:self.split(s)['births'].reverse())
 def test_wrong_formation_flags(self):self.reject(lambda s:self.split(s)['births'][0].update(flags=0x10000))
 def test_wrong_attachment_class(self):self.reject(lambda s:self.split(s)['slots'].__setitem__(1,0))
 def test_lost_native_member(self):self.reject(lambda s:self.split(s)['births'][0]['members'].pop())
 def test_wrong_source_pin(self):self.reject(lambda s:s['pins'].__setitem__(V.SOURCES[0],'0'*64))
 def test_missing_engine_callback_check(self):self.reject(lambda s:s['engine_tests'].pop(2))
if __name__=='__main__':unittest.main()
