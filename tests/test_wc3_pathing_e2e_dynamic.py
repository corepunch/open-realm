"""Dynamic/pursuit acceptance must retain intermediate state and coverage."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.e2e206_contract import validate
from verify_wc3_pathing_e2e_dynamic import FIXTURE
from verify_wc3_pathing_e2e_variants import engine_totals


class DynamicBaselineTests(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads(FIXTURE.read_text())
        self.manifest = json.loads((ROOT / 'tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json').read_text())

    def test_complete_original_contracts(self):
        self.assertEqual(len(validate(self.spec, self.manifest)), 3)

    def test_each_original_expectation_pin_is_required(self):
        for path in self.spec['pins']:
            spec = copy.deepcopy(self.spec)
            spec['pins'][path] = '0' * 64
            with self.assertRaises(ValueError):
                validate(spec, self.manifest)

    def test_missing_pursuit_dynamic_and_completion_fail(self):
        for index in range(2):
            for field in ('test', 'scope', 'completion'):
                spec = copy.deepcopy(self.spec)
                spec['categories'][index][field] = ''
                with self.assertRaises(ValueError):
                    validate(spec, self.manifest)
        spec = copy.deepcopy(self.spec)
        spec['categories'].pop()
        with self.assertRaises(ValueError):
            validate(spec, self.manifest)

    def test_weakened_intermediate_state_contract_fails(self):
        for category in self.spec['categories']:
            for identity in category['entries']:
                manifest = copy.deepcopy(self.manifest)
                next(row for row in manifest['entries'] if row['id'] == identity)['checks'] = {'passed': {'equal': True}}
                with self.assertRaises(ValueError):
                    validate(self.spec, manifest)

    def test_engine_report_requires_both_categories(self):
        good = '=== 400000/400000 assertions passed in 2 test(s) ==='
        self.assertEqual(engine_totals(good, 0, 2), 400000)
        for bad in ('', good + good, good.replace('2 test', '1 test'),
                    good.replace('400000/400000', '399999/400000')):
            with self.assertRaises(ValueError):
                engine_totals(bad, 0, 2)


if __name__ == '__main__':
    unittest.main()
