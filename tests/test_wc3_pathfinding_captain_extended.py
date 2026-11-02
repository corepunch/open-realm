"""Preserve the longer observation and reject invented cleanup of visible Follow."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_captain_extended_trace import verify_contract


class ExtendedCaptainTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-extended-1.27.json').read_text())

    def rejects(self, mutate):
        f = copy.deepcopy(self.fixture); mutate(f)
        with self.assertRaises(ValueError): verify_contract(f)

    def test_complete_extended_inputs_and_literal_header(self):
        verify_contract(self.fixture)
        h = ROOT / 'games/warcraft-3/game/tests/retail_captain_go_home_complete.h'
        self.assertEqual(hashlib.sha256(h.read_bytes()).hexdigest(), self.fixture['engine_header_sha256'])

    def test_standing_follow_is_not_natural_task_reclamation(self):
        self.rejects(lambda f: f.__setitem__('natural_task_reclamation', True))
        self.rejects(lambda f: f.__setitem__('persistent_follow_remains', False))

    def test_visible_completion_gate_cannot_be_cleared(self):
        for key, value in [('flags', 137216), ('member_flags', 0), ('missed', 33), ('completion', 1)]:
            self.rejects(lambda f: f['standing_owner'].__setitem__(key, value))

    def test_partial_and_unrepeated_captures_rejected(self):
        self.rejects(lambda f: f.__setitem__('primary_advances', 6000))
        self.rejects(lambda f: f.__setitem__('engine_commits', 7933))
        self.rejects(lambda f: f['cases'].pop())

    def test_scalar_owner_count_is_not_an_integer_six_phase_count(self):
        self.assertNotEqual(self.fixture['owner_callbacks'], self.fixture['primary_advances'] // 6)
        self.rejects(lambda f: f.__setitem__('owner_callbacks', 4001))

    def test_capped_observation_cannot_establish_standing_lifetime(self):
        self.rejects(lambda f: f['cases'][0]['metadata'].__setitem__('samples', f['cases'][0]['velocity_commits']))

    def test_whole_pathfinder_claim_rejected(self):
        self.rejects(lambda f: f.__setitem__('whole_retail_pathfinder', True))


if __name__ == '__main__': unittest.main()
