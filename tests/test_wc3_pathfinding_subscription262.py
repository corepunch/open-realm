"""Reject changed original subscription policy, incomplete captures and provenance."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_subscription262 as v
class SubscriptionTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def test_complete_contract(self):self.assertEqual(v.runtime(self.bundle,v.validate(self.spec))['actor_exemptions'],4)
 def reject(self,edit):
  b=copy.deepcopy(self.bundle);edit(b)
  with self.assertRaises(ValueError):v.runtime(b,self.spec)
 def test_missing_repeat_rejected(self):self.reject(lambda b:b['captures'].pop(0))
 def test_enabled_explicit_owner_rejected(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='notify-enter'and r['receiver']['identity']==1).__setitem__('ability_flags',0x4000))
 def test_missing_delivery_rejected(self):
  self.reject(lambda b:b['captures'][0]['rows'].pop(next(i for i,r in enumerate(b['captures'][0]['rows'])if r.get('event')=='notify-enter')))
 def test_control_marker_change_rejected(self):self.reject(lambda b:b['captures'][2].update(preload=b['captures'][2]['preload'].replace('order=0','order=1',1)))
 def test_missing_completion_rejected(self):self.reject(lambda b:b['captures'][0]['rows'].pop())
 def test_owner_environment_rejected(self):self.reject(lambda b:b['captures'][0]['rows'][0].update(env='A'))
 def test_instruction_omission_rejected(self):
  s=copy.deepcopy(self.spec);s['instructions'].pop(next(iter(s['instructions'])))
  with self.assertRaises(ValueError):v.validate(s)
 def test_source_pin_change_rejected(self):
  s=copy.deepcopy(self.spec);s['pins'][v.SOURCES[0]]='0'*64
  with self.assertRaises(ValueError):v.validate(s)
if __name__=='__main__':unittest.main()
