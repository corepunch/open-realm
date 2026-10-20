"""Guard/domain omissions and reference imbalance must not certify bridge parity."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_bridge265 as v
class BridgeTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(v.FIXTURE.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_complete(self):self.assertEqual(len(v.validate(self.spec)['rows']),132)
 def test_missing_negative_domain(self):self.reject(lambda s:s.update(rows=s['rows'][:66]))
 def test_stale_is_not_live(self):self.reject(lambda s:next(r for r in s['rows']if r['state']=='stale').update(dispatched=True))
 def test_pending_release_is_not_live(self):self.reject(lambda s:next(r for r in s['rows']if r['state']=='pending_release').update(dispatched=True))
 def test_reference_leak(self):self.reject(lambda s:s['rows'][0].update(balanced=False))
 def test_missing_pin(self):self.reject(lambda s:s['rows'][0].update(pin_references=[]))
 def test_wrong_event(self):self.reject(lambda s:s['rows'][0].update(event=0x40190066))
 def test_missing_instruction(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
 def test_changed_source(self):self.reject(lambda s:s['pins'].update({v.SOURCES[0]:'0'*64}))
 def test_wrong_game(self):self.reject(lambda s:s.update(game_sha256='0'*64))
if __name__=='__main__':unittest.main()
