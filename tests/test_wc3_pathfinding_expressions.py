"""Reject loss of actual compiled expression producers or whole movement evidence."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_expression_trace import render_header, verify_contract


class ChainedExpressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-chained-expressions-1.27.json').read_text())

    def test_complete_native_cases_and_engine_literal(self):
        verify_contract(self.fixture)
        self.assertEqual(len(self.fixture['cases']), 2)
        self.assertEqual((ROOT / 'games/warcraft-3/game/tests/retail_expressions.h').read_text(), render_header(self.fixture))

    def test_expression_order_cannot_be_associated_right(self):
        s = copy.deepcopy(self.fixture);s['expressions'][0]['input'] = 95
        with self.assertRaises(ValueError):verify_contract(s)

    def test_operand_side_effect_order_cannot_be_reversed(self):
        s = copy.deepcopy(self.fixture)
        a = next(i for i,r in enumerate(s['expression_sequence']) if r['event'] == 'expression-marker')
        s['expression_sequence'][a:a+3] = reversed(s['expression_sequence'][a:a+3])
        with self.assertRaises(ValueError):verify_contract(s)

    def test_integer_and_real_intermediates_are_distinct(self):
        s = copy.deepcopy(self.fixture)
        next(c for c in s['expressions'] if c['id'] == 'int_div_mul')['input'] = 9
        with self.assertRaises(ValueError):verify_contract(s)

    def test_public_timer_word_cannot_be_rounded_to_literal_point_one(self):
        s = copy.deepcopy(self.fixture);s['timers'][0]['timeout'] = 0x3dcccccc
        with self.assertRaises(ValueError):verify_contract(s)

    def test_move_destination_must_come_from_compiled_expression(self):
        s = copy.deepcopy(self.fixture);s['destination'][0] = 1680
        with self.assertRaises(ValueError):verify_contract(s)

    def test_entire_timer_and_commit_lifetime_is_required(self):
        for key in ('markers', 'motion', 'expressions'):
            s = copy.deepcopy(self.fixture);s[key].pop()
            with self.assertRaises(ValueError):verify_contract(s)

    def test_ancillary_base_map_timers_are_retained(self):
        s = copy.deepcopy(self.fixture);s['timers'].pop()
        with self.assertRaises(ValueError):verify_contract(s)


if __name__ == '__main__':unittest.main()
