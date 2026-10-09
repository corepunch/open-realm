"""Partial fine endpoints preserve source bits, coarse progress and explicit stop."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_captain_refill_trace import verify_contract


class CaptainRefillTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-refill-1.27.json').read_text())

    def rejects(self, mutate):
        f = copy.deepcopy(self.fixture)
        mutate(f)
        with self.assertRaises(ValueError):
            verify_contract(f)

    def test_verified_continuation_and_literal_tail(self):
        verify_contract(self.fixture)
        header = ROOT / 'games/warcraft-3/game/tests/retail_captain_go_home_refill.h'
        self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(), self.fixture['engine_header_sha256'])

    def test_repeat_is_required(self):
        self.rejects(lambda f: f['cases'].pop())

    def test_nearest_cell_cannot_replace_exact_source(self):
        self.rejects(lambda f: f['refill_witness']['points'].__setitem__(0, [0x43218000, 0x42460000]))

    def test_partial_search_must_exhaust_the_native_budget(self):
        self.rejects(lambda f: f['refill_witness']['search'].__setitem__(1, 700))

    def test_requested_goal_cannot_become_nearest_node(self):
        self.rejects(lambda f: f['refill_witness']['search'].__setitem__(4, [161, 49]))

    def test_one_point_reconstruction_cannot_be_truncated(self):
        self.rejects(lambda f: f['refill_witness'].__setitem__('truncated', True))

    def test_intermediate_completion_must_advance_coarse_index(self):
        self.rejects(lambda f: f['refill_witness']['after'].__setitem__(1, 7))

    def test_intermediate_completion_cannot_consume_retry(self):
        self.rejects(lambda f: f['refill_witness']['after'].__setitem__(9, 5))

    def test_intermediate_completion_invalidates_fine_index(self):
        self.rejects(lambda f: f['refill_witness']['after'].__setitem__(0, 0))

    def test_stopped_handoff_cannot_depend_on_turn_window(self):
        self.rejects(lambda f: f['stop_witness']['decision'].__setitem__(3, 0))

    def test_stopped_handoff_cannot_publish_moving_speed(self):
        self.rejects(lambda f: f['stop_witness']['outputs'].__setitem__(0, 0x40960000))

    def test_consumed_coarse_endpoint_must_be_retained(self):
        self.rejects(lambda f: f['stop_witness']['after'].__setitem__(1, 2))

    def test_natural_completion_remains_open(self):
        self.rejects(lambda f: f.__setitem__('natural_completion_remains_open', False))

    def test_scene_cannot_claim_whole_journey(self):
        self.rejects(lambda f: f.__setitem__('complete_scene_journeys', True))

    def test_observer_completion_does_not_prove_31_seconds(self):
        self.rejects(lambda f: f.__setitem__('engine_end_msec', 31000))

    def test_virtual_actor_motion_cannot_be_omitted(self):
        self.rejects(lambda f: f.__setitem__('virtual_commits', 0))

    def test_shared_footprint_extent_is_required(self):
        self.rejects(lambda f: f.__setitem__('shared_footprints', 624))

    def test_scene_cannot_claim_whole_pathfinder(self):
        self.rejects(lambda f: f.__setitem__('whole_retail_pathfinder', True))


if __name__ == '__main__':
    unittest.main()
