"""Timed-facing acceptance must preserve the frozen original word streams."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from verify_wc3_pathing_facing import FIXTURE, validate, markers, engine_totals


class FacingTests(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads(FIXTURE.read_text())

    def test_frozen_inputs_are_unchanged(self):
        validate(self.spec)

    def test_each_input_pin_is_required(self):
        for path in self.spec['pins']:
            spec = copy.deepcopy(self.spec)
            spec['pins'][path] = '0' * 64
            with self.assertRaises(ValueError):
                validate(spec)

    def test_counts_and_categories_cannot_be_weakened(self):
        for field in ('physical_count', 'visual_count', 'public_markers'):
            spec = copy.deepcopy(self.spec)
            spec[field] -= 1
            with self.assertRaises(ValueError):
                validate(spec)
        for field in ('captures', 'engine_tests', 'visual_counts'):
            spec = copy.deepcopy(self.spec)
            spec[field].pop()
            with self.assertRaises(ValueError):
                validate(spec)

    def test_engine_empty_partial_failed_or_duplicate_runs_fail(self):
        good = '=== 14308/14308 assertions passed in 3 test(s) ==='
        self.assertEqual(engine_totals(good, 0), 14308)
        for bad in ('', good + good, good.replace('3 test', '2 test'),
                    good.replace('14308/14308', '14307/14308'),
                    '=== 0/0 assertions passed in 0 test(s) ==='):
            with self.assertRaises(ValueError):
                engine_totals(bad, 0)
        with self.assertRaises(ValueError):
            engine_totals(good, 1)

    def test_public_output_requires_complete_marker(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'preload.txt'
            rows = ['call Preload( "F207 tick=%d label=sample" )' % i for i in range(88)]
            path.write_text('\n'.join(rows + ['call Preload( "F207 tick=80 label=complete" )']))
            self.assertEqual(len(markers(path)), 89)
            path.write_text('\n'.join(rows))
            with self.assertRaises(ValueError):
                markers(path)


if __name__ == '__main__':
    unittest.main()
