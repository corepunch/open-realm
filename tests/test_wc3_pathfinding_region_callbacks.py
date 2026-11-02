"""Reject incomplete/reordered original region callback and movement contracts."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_region_callbacks_trace import verify_contract,render_header

class RegionCallbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-region-callbacks-1.27.json').read_text())
    def test_complete_contract_and_literal_engine_inputs(self):
        verify_contract(self.fixture)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_region_callbacks.h').read_text(),render_header(self.fixture))
        self.assertEqual(len(self.fixture['geometry_controls']),18)
    def test_public_callback_order_cannot_change(self):
        s=copy.deepcopy(self.fixture);i=next(i for i,m in enumerate(s['markers'])if 'label=callback_new_move 'in m)
        s['markers'][i],s['markers'][i+1]=s['markers'][i+1],s['markers'][i]
        with self.assertRaises(ValueError):verify_contract(s)
    def test_teleport_must_clear_old_velocity(self):
        s=copy.deepcopy(self.fixture);next(x for x in s['states']if 'label=callback_teleport 'in x['marker'])['state'][3]=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_removal_cannot_expose_old_position(self):
        s=copy.deepcopy(self.fixture);next(x for x in s['states']if not x['alive'])['world'][0]=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_removal_cannot_commit_later(self):
        s=copy.deepcopy(self.fixture);s['motion'][-1][1]=0x41f00000
        with self.assertRaises(ValueError):verify_contract(s)
    def test_flat_support_is_explicit(self):
        s=copy.deepcopy(self.fixture);s['states'][3]['world'][2]=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_complete_lifetimes_required(self):
        for key in('motion','states','chains','destinations','markers'):
            s=copy.deepcopy(self.fixture);s[key].pop()
            with self.assertRaises(ValueError):verify_contract(s)
    def test_modified_committed_word_rejected(self):
        s=copy.deepcopy(self.fixture);s['motion'][300][2]^=1
        with self.assertRaises(ValueError):verify_contract(s)

if __name__=='__main__':unittest.main()
