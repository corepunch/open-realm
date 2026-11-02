"""Private approach evidence cannot certify the remaining mixed shared journey."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_approach_trace import render_header,verify_contract


class CaptainApproachTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-approach-1.27.json').read_text())

    def test_complete_native_reference_keeps_bounded_engine_scope(self):
        verify_contract(self.fixture)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_captain_thirteen_mixed.h').read_text(),render_header(self.fixture))

    def test_collision_multiple_cannot_replace_authored_approach(self):
        f=copy.deepcopy(self.fixture);f['initial_arrival_ranges'][0][1]=0x411d8000
        with self.assertRaises(ValueError):verify_contract(f)

    def test_attack_range_is_an_independent_input(self):
        f=copy.deepcopy(self.fixture);f['authored_ranges'][0][4]=0x42c80000
        with self.assertRaises(ValueError):verify_contract(f)

    def test_reentry_preserves_authentic_unit_generation(self):
        f=copy.deepcopy(self.fixture);f['authored_ranges'][-1][0]=0
        with self.assertRaises(ValueError):verify_contract(f)

    def test_full_native_capture_does_not_imply_full_engine_parity(self):
        f=copy.deepcopy(self.fixture);f['whole_engine_parity']=True
        with self.assertRaises(ValueError):verify_contract(f)

    def test_shared_owner_gap_cannot_be_hidden(self):
        f=copy.deepcopy(self.fixture);f['shared_owner_remains_open']=False
        with self.assertRaises(ValueError):verify_contract(f)

    def test_thirteenth_mover_cannot_be_dropped(self):
        f=copy.deepcopy(self.fixture);f['motion']=[r for r in f['motion'] if r[0]!=12]
        with self.assertRaises(ValueError):verify_contract(f)


if __name__=='__main__':unittest.main()
