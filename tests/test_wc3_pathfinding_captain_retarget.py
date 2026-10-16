"""Accepted target changes reset pending waits before fine refills."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_captain_retarget_trace import verify_contract


class CaptainRetargetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-retarget-1.27.json').read_text())

    def rejects(self, mutate):
        f = copy.deepcopy(self.fixture)
        mutate(f)
        with self.assertRaises(ValueError):
            verify_contract(f)

    def test_verified_continuation_and_literal_tail(self):
        verify_contract(self.fixture)
        header = ROOT / 'games/warcraft-3/game/tests/retail_captain_go_home_retry.h'
        self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(), self.fixture['engine_header_sha256'])

    def test_repeat_is_required(self):
        self.rejects(lambda f: f['cases'].pop())

    def test_original_pending_wait_must_be_witnessed(self):
        self.rejects(lambda f: f['reset_witness'].__setitem__('before_delay', 0))

    def test_wait_must_clear_on_accepted_change(self):
        self.rejects(lambda f: f['reset_witness'].__setitem__('after_delay', 20))

    def test_changed_destination_is_required(self):
        self.rejects(lambda f: f['reset_witness'].__setitem__('changed', 0))

    def test_destination_readiness_is_required(self):
        self.rejects(lambda f: f['reset_witness'].__setitem__('ready', 0))

    def test_other_timestamp_producers_are_not_claimed(self):
        self.rejects(lambda f: f['reset_witness'].__setitem__('timestamps', [1820, 1820]))

    def test_new_heading_cannot_retain_previous_route(self):
        self.rejects(lambda f: f['reset_witness']['outputs'].__setitem__(1, 0x3fa64d14))

    def test_one_point_refill_is_still_open(self):
        self.rejects(lambda f: f.__setitem__('one_point_refill_remains_open', False))

    def test_full_journey_cannot_be_claimed(self):
        self.rejects(lambda f: f.__setitem__('complete_scene_journeys', True))

    def test_later_travel_cannot_be_claimed(self):
        self.rejects(lambda f: f.__setitem__('engine_end_msec', 31000))

    def test_virtual_commits_cannot_be_dropped(self):
        self.rejects(lambda f: f.__setitem__('virtual_commits', 0))

    def test_scene_does_not_prove_entire_pathfinder(self):
        self.rejects(lambda f: f.__setitem__('whole_retail_pathfinder', True))


if __name__ == '__main__':
    unittest.main()
