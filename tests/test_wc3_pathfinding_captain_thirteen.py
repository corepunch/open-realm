"""The captain batch boundary preserves the thirteenth physical recruit."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_thirteen_trace import render_header,verify_contract


class CaptainThirteenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-thirteen-1.27.json').read_text())

    def test_complete_contract_and_literal_scene_geometry(self):
        verify_contract(self.fixture)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_captain_thirteen.h').read_text(),render_header(self.fixture))

    def test_twelfth_callback_cannot_publish_all_members(self):
        f=copy.deepcopy(self.fixture)
        [r for r in f['admission'] if r[0]=='captain-range-enter-end'][11][3][3]=13
        with self.assertRaises(ValueError):verify_contract(f)

    def test_thirteenth_member_requires_second_request(self):
        f=copy.deepcopy(self.fixture)
        [r for r in f['admission'] if r[0]=='captain-prepare-begin'][-1][3]=0
        with self.assertRaises(ValueError):verify_contract(f)

    def test_both_requests_retain_common_shared_wrapper(self):
        f=copy.deepcopy(self.fixture)
        [r for r in f['admission'] if r[0]=='captain-prepare-begin'][-1][-1]=1
        with self.assertRaises(ValueError):verify_contract(f)

    def test_native_newest_batch_runs_first(self):
        f=copy.deepcopy(self.fixture);shared=[r for r in f['footprints'] if r[1] is not None]
        shared[0][3]=shared[1][3]
        with self.assertRaises(ValueError):verify_contract(f)

    def test_placement_requires_original_support_levels(self):
        f=copy.deepcopy(self.fixture);f['terrain_support']['level_runs'][0][1]^=1
        with self.assertRaises(ValueError):verify_contract(f)

    def test_thirteenth_physical_journey_cannot_be_omitted(self):
        f=copy.deepcopy(self.fixture);f['motion']=[r for r in f['motion'] if r[0]!=12]
        with self.assertRaises(ValueError):verify_contract(f)


if __name__=='__main__':unittest.main()
