"""Reject incomplete native mode changes and missing active flight records."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_movement_modes_trace import verify_contract,render_header

class MovementModeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-movement-modes-1.27.json').read_text())
    def test_complete_contract_and_literal_engine_inputs(self):
        verify_contract(self.fixture)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_movement_modes.h').read_text(),render_header(self.fixture))
    def test_ordering_required(self):
        s=copy.deepcopy(self.fixture);s['markers'][40],s['markers'][80]=s['markers'][80],s['markers'][40]
        with self.assertRaises(ValueError):verify_contract(s)
    def test_teleport_cancels_velocity(self):
        s=copy.deepcopy(self.fixture);s['states'][3]['state'][3]=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_flight_records_are_active(self):
        s=copy.deepcopy(self.fixture);s['states'][3]['spatial']['chain']=[]
        with self.assertRaises(ValueError):verify_contract(s)
    def test_class_and_query_are_distinct(self):
        s=copy.deepcopy(self.fixture);s['states'][3]['spatial']['pathFlags']=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_category_zero_is_not_missing_membership(self):
        s=copy.deepcopy(self.fixture);s['states'][3]['category']=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_complete_lifetimes_required(self):
        for key in('motion','states','chains','destinations','markers','routes','spatial_queries','mode_phases'):
            s=copy.deepcopy(self.fixture);s[key].pop()
            with self.assertRaises(ValueError):verify_contract(s)
    def test_modified_committed_word_rejected(self):
        s=copy.deepcopy(self.fixture);s['motion'][300][2]^=1
        with self.assertRaises(ValueError):verify_contract(s)

if __name__=='__main__':unittest.main()
