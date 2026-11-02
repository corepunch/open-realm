"""Reject premature three-member admission and reinitialized survivor retries."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_three_trace import render_header,verify_contract


class CaptainThreeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-three-1.27.json').read_text())

    def test_complete_contract_and_engine_reference(self):
        verify_contract(self.fixture)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_captain_three.h').read_text(),render_header(self.fixture))

    def test_second_callback_cannot_publish_full_roster(self):
        f=copy.deepcopy(self.fixture)
        [r for r in f['admission'] if r[0]=='captain-range-enter-end'][1][3][3]=3
        with self.assertRaises(ValueError):verify_contract(f)

    def test_third_recruit_cannot_be_dropped(self):
        f=copy.deepcopy(self.fixture);f['motion']=[r for r in f['motion'] if r[0]!=2]
        with self.assertRaises(ValueError):verify_contract(f)

    def test_survivor_retry_budget_cannot_be_reinitialized(self):
        f=copy.deepcopy(self.fixture)
        [r for r in f['lifecycle'] if r['event']=='retry-result'][2]['before']=0
        with self.assertRaises(ValueError):verify_contract(f)

    def test_survivor_retry_cannot_redraw_random_owner(self):
        f=copy.deepcopy(self.fixture)
        [r for r in f['lifecycle'] if r['event']=='retry-result'][2]['ownerAfter'][0]^=1
        with self.assertRaises(ValueError):verify_contract(f)

    def test_terminal_retry_requires_forced_arrival(self):
        f=copy.deepcopy(self.fixture)
        next(r for r in f['lifecycle'] if r['event']=='force-arrival')['after']=0
        with self.assertRaises(ValueError):verify_contract(f)


if __name__=='__main__':unittest.main()
