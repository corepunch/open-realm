"""Cross-feature acceptance rejects missing routes, stale pins and empty engine runs."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from verify_wc3_pathing_e2e import engine_totals, validate


class E2EBaselineTests(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads((ROOT / 'tools/ghidra/fixtures/retail-e2e-baseline203-1.27.json').read_text())

    def test_literal_routes_match_the_unchanged_originals(self):
        self.assertEqual(validate(self.spec), [1, 6, 30])

    def test_each_variant_and_boundary_is_required(self):
        for field in ('constructed_routes', 'motion_commits', 'saved_motion_commits'):
            for i in range(3):
                spec = copy.deepcopy(self.spec)
                spec['scenarios'][i][field] += 1
                with self.assertRaises(ValueError):
                    validate(spec)
        for key in ('repetitions', 'base', 'engine_editions'):
            spec = copy.deepcopy(self.spec)
            del spec[key]
            with self.assertRaises((ValueError, KeyError)):
                validate(spec)
        spec = copy.deepcopy(self.spec)
        spec['scenarios'].reverse()
        with self.assertRaises(ValueError):
            validate(spec)

    def test_retail_pins_cannot_be_silently_updated(self):
        for path in self.spec['pins']:
            spec = copy.deepcopy(self.spec)
            spec['pins'][path] = '0' * 64
            with self.assertRaises(ValueError):
                validate(spec)
        spec = copy.deepcopy(self.spec)
        spec['pins']['../not-a-fixture'] = '0' * 64
        with self.assertRaises(ValueError):
            validate(spec)

    def test_engine_zero_tests_failures_duplicate_and_absent_summaries_are_rejected(self):
        good = '=== 149000/149000 assertions passed in 3 test(s) ==='
        self.assertEqual(engine_totals(good, 0), 149000)
        for log in ('', '=== 0/0 assertions passed in 0 test(s) ===',
                    '=== 149000/149001 assertions passed in 3 test(s), 1 failed ===',
                    good + '\n' + good, good.replace('3 test', '2 test')):
            with self.assertRaises(ValueError):
                engine_totals(log, 0)
        with self.assertRaises(ValueError):
            engine_totals(good, 1)


if __name__ == '__main__':
    unittest.main()
