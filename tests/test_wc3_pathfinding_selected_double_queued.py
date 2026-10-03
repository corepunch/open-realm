"""Two pending Shift moves preserve submission history through FIFO activation."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_selected_queued_trace import digest, render_header, verify_latest_neighbors


class SelectedDoubleQueuedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-selected-double-queued-1.27.json').read_text())

    def test_latest_submission_survives_both_fifo_activations(self):
        for case in self.fixture['cases']:
            points = [r['point'] for r in case['producer'] if r['event'] == 'player-point-action-begin']
            searches = [r for r in case['neighbors'] if r['event'] == 'move-previous-cohort-search']
            latest = searches[0]['before']['previous']
            self.assertEqual(latest, [1305,1331])
            self.assertEqual([r['counter'] for r in verify_latest_neighbors(case['neighbors'], points, latest)],
                             [1240,1241,1307,1317])

    def test_old_history_wrong_point_category_and_self_matches_are_rejected(self):
        case = self.fixture['cases'][0]
        points = [r['point'] for r in case['producer'] if r['event'] == 'player-point-action-begin']
        for kind in ('old','point','category','self','result','missing'):
            with self.subTest(kind=kind):
                rows = copy.deepcopy(case['neighbors'])
                searches = [r for r in rows if r['event'] == 'move-previous-cohort-search']
                accepted = next(r for r in rows if r.get('accepted'))
                if kind == 'old': searches[0]['before']['previous'] = [1133,1327]
                elif kind == 'point': searches[2]['point'] = points[0]
                elif kind == 'category': searches[0]['before']['category'] = 0
                elif kind == 'self': accepted['candidate']['unit'] = accepted['source']['unit']
                elif kind == 'result': accepted['result'] = 1
                else: rows.pop()
                with self.assertRaises(ValueError): verify_latest_neighbors(rows,points,[1305,1331])

    def test_five_cohorts_keep_original_owner_visit_order(self):
        visits = [r[0] for r in self.fixture['owner_order'] if r[1:] == ['pair-group-phase-begin','decide']]
        self.assertEqual([visits.count(i) for i in range(5)], [184,1,76,10,24])
        self.assertEqual(len(visits),295)

    def test_both_inputs_differ_but_all_absolute_motion_words_repeat(self):
        inputs = [[r['clock'] for r in c['producer'] if r['event'] == 'player-point-action-begin']
                  for c in self.fixture['cases']]
        self.assertNotEqual(inputs[0],inputs[1])
        self.assertEqual(len(self.fixture['engine_motion']),555)
        self.assertEqual(digest(self.fixture['engine_motion']),
                         'ea9e66c5f2a87a4ec7051b88a9c86b9e4a6bed03b5bbc347daada1580434137e')

    def test_engine_header_keeps_four_inputs_and_complete_motion(self):
        self.assertEqual((ROOT / 'games/warcraft-3/game/tests/retail_selected_double_queued.h').read_text(),
                         render_header(self.fixture))


if __name__ == '__main__':
    unittest.main()
