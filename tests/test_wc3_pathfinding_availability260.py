"""Reject incomplete or behavior-changing retail availability evidence."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_availability260 as v
class AvailabilityTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.s=json.loads(v.FIXTURE.read_text());cls.b=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def test_original_contract(self):self.assertEqual(v.runtime(self.b,v.validate(self.s))['notifications'],40)
 def test_missing_repeat_fails(self):
  b=copy.deepcopy(self.b);b['captures'].pop(0)
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_control_hook_fails(self):
  b=copy.deepcopy(self.b);b['captures'][2]['rows'].insert(1,dict(event='module'))
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_changed_range_result_fails(self):
  b=copy.deepcopy(self.b);next(r for r in b['captures'][0]['rows']if r.get('event')=='range')['accepted']=0
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_missing_exemption_fails(self):
  b=copy.deepcopy(self.b);rows=b['captures'][0]['rows'];rows.remove(next(r for r in rows if r.get('event')=='begin'))
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_public_order_change_fails(self):
  b=copy.deepcopy(self.b);b['captures'][2]['preload']=b['captures'][2]['preload'].replace('order=851986','order=0')
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_wrong_producer_fails(self):
  b=copy.deepcopy(self.b);next(r for r in b['captures'][0]['rows']if r.get('event')=='available-enter')['caller']='movement-commit'
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_source_pins_required(self):
  s=copy.deepcopy(self.s);s['pins']={}
  with self.assertRaises(ValueError):v.validate(s)
if __name__=='__main__':unittest.main()
