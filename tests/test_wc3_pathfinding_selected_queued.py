"""Original Shift witnesses distinguish pending requests and rebuilt physical cohorts."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_selected_queued_trace import digest, render_header, verify_neighbors


class SelectedQueuedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-selected-queued-1.27.json').read_text())

    def test_original_staggered_neighbor_acquisition(self):
        for case in self.fixture['cases']:
            point = next(r['point'] for r in case['producer'] if r['event'] == 'player-point-action-begin')
            searches = verify_neighbors(case['neighbors'], point)
            self.assertEqual([r['counter'] for r in searches], [1240, 1241])
            self.assertEqual([r['result'] for r in searches], [0, 1])

    def test_other_requests_categories_points_and_callback_results_are_rejected(self):
        case = self.fixture['cases'][0]
        point = next(r['point'] for r in case['producer'] if r['event'] == 'player-point-action-begin')
        for kind in ('previous', 'category', 'point', 'output', 'accepted', 'result', 'self', 'missing'):
            with self.subTest(kind=kind):
                rows = copy.deepcopy(case['neighbors'])
                searches = [r for r in rows if r['event'] == 'move-previous-cohort-search']
                accepted = next(r for r in rows if r.get('accepted'))
                if kind == 'previous': searches[1]['before']['previous'][1] ^= 1
                elif kind == 'category': searches[1]['before']['category'] ^= 1
                elif kind == 'point': searches[1]['point'][0] ^= 1
                elif kind == 'output': searches[1]['output'] = '0x0'
                elif kind == 'accepted': accepted['accepted'] = 0
                elif kind == 'result': accepted['result'] = 1
                elif kind == 'self': accepted['candidate']['unit'] = accepted['source']['unit']
                else: rows.pop()
                with self.assertRaises(ValueError): verify_neighbors(rows, point)

    def test_three_cohorts_keep_original_owner_visit_order(self):
        visits = [r[0] for r in self.fixture['owner_order']
                  if r[1:] == ['pair-group-phase-begin', 'decide']]
        self.assertEqual([visits.count(i) for i in range(3)], [184, 1, 76])
        temporary = visits.index(1)
        self.assertEqual(visits[temporary:temporary+3], [1, 0, 2])

    def test_every_motion_word_and_supplied_input_clock_is_retained(self):
        self.assertEqual(len(self.fixture['engine_motion']), 510)
        a, b = [next(r for r in c['producer'] if r['event'] == 'player-point-action-begin')
                for c in self.fixture['cases']]
        self.assertNotEqual(a['clock'], b['clock'])
        self.assertEqual(a['point'], b['point'])
        self.assertEqual(digest(self.fixture['engine_motion']),
                         'a8020af581b9aa7fa0d83d1474baffae9e2ae9d73166306c3e6ededc32f03c14')

    def test_engine_header_keeps_both_inputs_and_complete_motion(self):
        self.assertEqual((ROOT / 'games/warcraft-3/game/tests/retail_selected_queued.h').read_text(),
                         render_header(self.fixture))


if __name__ == '__main__':
    unittest.main()
