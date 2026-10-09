"""Reject evidence that loses the real overlap producer or either chain order."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from make_wc3_pathfinding_map import passage_map_member
from verify_wc3_target_overlap_trace import render_header, verify_contract


class TargetOverlapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-target-overlap-1.27.json').read_text())

    def test_complete_native_journey_and_engine_literal(self):
        verify_contract(self.fixture)
        self.assertEqual(len(self.fixture['cases']), 2)
        self.assertEqual((ROOT / 'games/warcraft-3/game/tests/retail_target_overlap.h').read_text(), render_header(self.fixture))

    def test_foreign_first_must_exhaust_budget(self):
        s = copy.deepcopy(self.fixture)
        next(r for r in s['lifecycle'] if r['event'] == 'search' and r['kind'] == 'fine')['result'] = 122
        with self.assertRaises(ValueError): verify_contract(s)

    def test_target_first_must_terminate_early(self):
        s = copy.deepcopy(self.fixture)
        [r for r in s['lifecycle'] if r['event'] == 'search' and r['kind'] == 'fine'][1]['pops'] = 701
        with self.assertRaises(ValueError): verify_contract(s)

    def test_reinsertion_must_reverse_actual_chain(self):
        s = copy.deepcopy(self.fixture); s['chains'][4]['records'].reverse()
        with self.assertRaises(ValueError): verify_contract(s)

    def test_removal_must_remove_foreign_links(self):
        s = copy.deepcopy(self.fixture); s['chains'][-1]['records'].append(s['chains'][1]['records'][1])
        with self.assertRaises(ValueError): verify_contract(s)

    def test_complete_timer_and_motion_extents(self):
        for key in ('markers', 'motion'):
            s = copy.deepcopy(self.fixture); s[key].pop()
            with self.assertRaises(ValueError): verify_contract(s)

    def test_empty_public_map_does_not_reuse_passage_obstacles(self):
        terrain = passage_map_member('war3map.wpm', b'', blocked=[])
        self.assertEqual(len(terrain), 16 + 4096)
        self.assertEqual(terrain[16:], bytes(4096))


if __name__ == '__main__': unittest.main()
