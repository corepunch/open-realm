"""Moving captain evidence must preserve virtual identity and bounded travel."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_captain_go_home_trace import verify_contract


class CaptainGoHomeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-go-home-1.27.json').read_text())

    def rejects(self, mutate):
        f = copy.deepcopy(self.fixture)
        mutate(f)
        with self.assertRaises(ValueError):
            verify_contract(f)

    def test_verified_prefix_and_literal_engine_reference(self):
        verify_contract(self.fixture)
        header = ROOT / 'games/warcraft-3/game/tests/retail_captain_go_home.h'
        self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(), self.fixture['engine_header_sha256'])

    def test_repeat_is_required(self):
        self.rejects(lambda f: f['cases'].pop())

    def test_full_journey_cannot_be_claimed(self):
        self.rejects(lambda f: f.__setitem__('complete_scene_journeys', True))

    def test_later_retry_is_still_open(self):
        self.rejects(lambda f: f.__setitem__('private_retry_continuation_remains_open', False))

    def test_prefix_cannot_be_extended_without_engine_parity(self):
        self.rejects(lambda f: f.__setitem__('engine_end_msec', 31000))

    def test_virtual_actor_is_not_a_footman(self):
        self.rejects(lambda f: f['virtual_profile'].__setitem__(1, 202))

    def test_virtual_motion_cannot_be_omitted(self):
        self.rejects(lambda f: f.__setitem__('virtual_commits', 0))

    def test_retained_request_range_is_not_actor_arrival_range(self):
        self.rejects(lambda f: f.__setitem__('retained_request_range', 200))

    def test_all_entered_callback_tightens_actor_range(self):
        self.rejects(lambda f: f['virtual_ranges'].__setitem__(-1, 0x417a0000))

    def test_capped_capture_cannot_certify_the_prefix(self):
        self.rejects(lambda f: f['cases'][0].__setitem__('velocity_commits', 30000))

    def test_native_reuse_requires_a_new_generation(self):
        self.rejects(lambda f: f['identities'].__setitem__(1, f['identities'][0]))

    def test_scene_does_not_prove_entire_pathfinder(self):
        self.rejects(lambda f: f.__setitem__('whole_retail_pathfinder', True))


if __name__ == '__main__':
    unittest.main()
