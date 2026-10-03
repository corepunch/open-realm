"""Reject incomplete original pathing-toggle and pause lifecycle evidence."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_movement_bypasses_trace import verify_contract,render_header

class MovementBypassTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-movement-bypasses-1.27.json').read_text())
    def test_complete_contract_and_literal_engine_inputs(self):
        verify_contract(self.fixture)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_movement_bypasses.h').read_text(),render_header(self.fixture))
    def test_native_ordering_required(self):
        s=copy.deepcopy(self.fixture);i=next(i for i,m in enumerate(s['markers'])if 'label=paused_displacement 'in m)
        j=next(j for j,m in enumerate(s['markers'])if 'label=resumed 'in m)
        s['markers'][i],s['markers'][j]=s['markers'][j],s['markers'][i]
        with self.assertRaises(ValueError):verify_contract(s)
    def test_pause_must_stop_velocity(self):
        s=copy.deepcopy(self.fixture);next(r for r in s['states']if 'label=paused 'in r['marker'])['state'][3]=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_unpause_reports_false_before_head_reactivation(self):
        s=copy.deepcopy(self.fixture);next(r for r in s['states']if 'label=resumed 'in r['marker'])['paused']=True
        with self.assertRaises(ValueError):verify_contract(s)
    def test_disabled_query_keeps_occupied_category(self):
        s=copy.deepcopy(self.fixture);next(r for r in s['producers']if r['event']=='movement-mask-publication'and not r['pathMask'])['objectCategory']=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_flat_support_is_explicit(self):
        s=copy.deepcopy(self.fixture);s['states'][3]['world'][2]=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_complete_lifetimes_and_routes_required(self):
        for key in('motion','states','chains','destinations','markers','routes'):
            s=copy.deepcopy(self.fixture);s[key].pop()
            with self.assertRaises(ValueError):verify_contract(s)
    def test_modified_committed_word_rejected(self):
        s=copy.deepcopy(self.fixture);s['motion'][300][2]^=1
        with self.assertRaises(ValueError):verify_contract(s)

if __name__=='__main__':unittest.main()
