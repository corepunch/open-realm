"""Complete last-binding Stop evidence must retain teardown order and AI gate."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_captain_last_binding_trace import verify_contract


class CaptainLastBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-last-binding-1.27.json').read_text())

    def rejects(self, mutate):
        f = copy.deepcopy(self.fixture)
        mutate(f)
        with self.assertRaises(ValueError):
            verify_contract(f)

    def test_complete_journey_and_literal_engine_reference(self):
        verify_contract(self.fixture)
        header = ROOT / 'games/warcraft-3/game/tests/retail_captain_cancel_all.h'
        self.assertEqual(hashlib.sha256(header.read_bytes()).hexdigest(), self.fixture['engine_header_sha256'])

    def test_repeat_is_required(self):
        self.rejects(lambda f: f['cases'].pop())

    def test_full_motion_is_required(self):
        self.rejects(lambda f: f.__setitem__('engine_commits', 3471))

    def test_bound_groups_survive_first_post_stop_prepass(self):
        self.rejects(lambda f: f['cancellation']['publication'][-2][2].__setitem__(0, 0))

    def test_collection_requires_zero_references(self):
        self.rejects(lambda f: f['cancellation']['publication'][-1][2].__setitem__(0, 1))

    def test_collection_clock_cannot_move_one_update_earlier(self):
        self.rejects(lambda f: f['cancellation']['publication'][-1][1].__setitem__(0, 0x411359ee))

    def test_collection_invalidates_original_identity(self):
        self.rejects(lambda f: f['cancellation']['publication'][-1].__setitem__(4, [1471, 1941]))

    def test_second_ai_call_does_not_replay_main(self):
        self.rejects(lambda f: f['cancellation']['markers'].append('PATHCAPTAIN home begin'))

    def test_second_ai_call_still_loads_sources(self):
        self.rejects(lambda f: f['cancellation']['natives'].pop(-4))

    def test_remove_retarget_and_reuse_are_not_claimed(self):
        self.rejects(lambda f: f.__setitem__('remove_retarget_reuse_remain_open', False))

    def test_scene_does_not_prove_entire_pathfinder(self):
        self.rejects(lambda f: f.__setitem__('whole_retail_pathfinder', True))


if __name__ == '__main__':
    unittest.main()
