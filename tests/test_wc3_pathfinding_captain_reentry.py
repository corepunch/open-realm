"""Full captain journey evidence keeps logical membership and physical groups distinct."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_captain_reentry_trace import references, verify_contract, render_header


class CaptainReentryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / 'tools/ghidra/fixtures/retail-captain-reentry-1.27.json'
        cls.fixture = json.loads(path.read_text())
        cls.departure, cls.shared = references(cls.fixture, path.parent)

    def rejects(self, mutate):
        f = copy.deepcopy(self.fixture)
        mutate(f)
        with self.assertRaises(ValueError):
            verify_contract(f, self.departure, self.shared)

    def test_complete_scene_and_literal_engine_reference(self):
        verify_contract(self.fixture, self.departure, self.shared)
        self.assertEqual(render_header(self.fixture, self.shared),
                         (ROOT / 'games/warcraft-3/game/tests/retail_captain_reentry_shared.h').read_text())

    def test_physical_completion_cannot_remove_logical_inner_members(self):
        self.rejects(lambda f: f['logical_deadlines'][-2].__setitem__(2, 0))

    def test_second_publication_requires_all_thirteen(self):
        self.rejects(lambda f: f['logical_deadlines'][-1].__setitem__(4, 12))

    def test_second_publication_retains_exact_deadline(self):
        self.rejects(lambda f: f['logical_deadlines'][-1].__setitem__(1, 0x41800000))

    def test_both_physical_shared_generations_are_required(self):
        self.rejects(lambda f: f.__setitem__('shared_generations', 1))

    def test_complete_motion_cannot_be_truncated_to_old_prefix(self):
        self.rejects(lambda f: f.__setitem__('engine_commits', 4867))

    def test_capture_provenance_is_bound_to_observed_membership(self):
        self.rejects(lambda f: f['cases'][0].__setitem__('trace_sha256', '0' * 64))

    def test_scene_completion_cannot_claim_the_whole_pathfinder(self):
        self.rejects(lambda f: f.__setitem__('whole_retail_pathfinder', True))


if __name__ == '__main__':
    unittest.main()
