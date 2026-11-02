"""Idle Shift starts native shared movement without a pending current order."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_selected_point_trace import render_header, verify_idle_shift


class SelectedIdleShiftTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-selected-idle-shift-1.27.json').read_text())

    def test_both_original_empty_heads_dispatch_immediately(self):
        for case in self.fixture['cases']:
            verify_idle_shift(case['idle_orders'], case['metadata'])
            self.assertEqual(case['packet_flags'], 9)
            self.assertEqual([r['countAfter'] for r in case['idle_orders']], [1, 1])
            self.assertTrue(all(r['after'] != [-1, -1] for r in case['idle_orders']))

    def test_shift_modifier_and_empty_current_orders_are_required(self):
        case = self.fixture['cases'][0]
        for kind in ('modifier', 'head', 'before_count', 'after_count', 'missing'):
            with self.subTest(kind=kind):
                rows, metadata = copy.deepcopy(case['idle_orders']), copy.deepcopy(case['metadata'])
                if kind == 'modifier': metadata['pointInput']['shift'] = False
                elif kind == 'head': rows[0]['before'][0] = 0
                elif kind == 'before_count': rows[0]['countBefore'] = 1
                elif kind == 'after_count': rows[0]['countAfter'] = 2
                else: rows.pop()
                with self.assertRaises(ValueError): verify_idle_shift(rows, metadata)

    def test_complete_independent_captures_repeat_every_absolute_motion_word(self):
        a, b = self.fixture['cases']
        self.assertNotEqual(a['trace_sha256'], b['trace_sha256'])
        self.assertEqual(len(a['engine_motion']), 228)
        self.assertEqual(a['engine_motion'], b['engine_motion'])

    def test_earlier_input_clock_preserves_its_own_position_rounding(self):
        late = json.loads((ROOT / 'tools/ghidra/fixtures/retail-selected-point-1.27.json').read_text())
        a, b = self.fixture['cases'][0]['engine_motion'], late['cases'][0]['engine_motion']
        self.assertEqual(a[0][2:], b[0][2:])
        self.assertNotEqual(a[2][3], b[2][3])
        self.assertNotEqual(a[0][1], b[0][1])

    def test_engine_header_retains_idle_input_and_motion_words(self):
        self.assertEqual((ROOT / 'games/warcraft-3/game/tests/retail_selected_idle_shift.h').read_text(),
                         render_header(self.fixture))


if __name__ == '__main__': unittest.main()
