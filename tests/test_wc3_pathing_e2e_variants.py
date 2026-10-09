"""Combined baseline refuses stale expectations and untested feature categories."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.e2e205_contract import validate
from verify_wc3_pathing_e2e_variants import engine_totals, FIXTURE


class CombinedVariantTests(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads(FIXTURE.read_text())
        self.manifest = json.loads((ROOT / 'tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json').read_text())

    def test_complete_unchanged_original_bindings(self):
        self.assertEqual(len(validate(self.spec, self.manifest)), 6)

    def test_missing_reordered_and_empty_categories_fail(self):
        for change in ('omit', 'reorder', 'scope', 'test'):
            spec = copy.deepcopy(self.spec)
            if change == 'omit':
                spec['categories'].pop()
            elif change == 'reorder':
                spec['categories'].reverse()
            elif change == 'scope':
                spec['categories'][0]['scope'] = ''
            else:
                spec['categories'][0]['test'] = 'wc3_e2e205.no_tests'
            with self.assertRaises(ValueError):
                validate(spec, self.manifest)

    def test_every_original_literal_pin_is_required(self):
        for path in self.spec['pins']:
            spec = copy.deepcopy(self.spec)
            spec['pins'][path] = '0' * 64
            with self.assertRaises(ValueError):
                validate(spec, self.manifest)

    def test_changed_retirement_retry_and_motion_contracts_fail(self):
        selected = {identity for row in self.spec['categories'] for identity in row['entries']}
        for identity in selected:
            manifest = copy.deepcopy(self.manifest)
            next(row for row in manifest['entries'] if row['id'] == identity)['checks'] = {'passed': {'equal': True}}
            with self.assertRaises(ValueError):
                validate(self.spec, manifest)

    def test_empty_failed_or_duplicate_engine_reports_fail(self):
        good = '=== 1700000/1700000 assertions passed in 3 test(s) ==='
        self.assertEqual(engine_totals(good, 0), 1700000)
        for bad in ('', good + '\n' + good, '=== 0/0 assertions passed in 0 test(s) ===',
                    good.replace('3 test', '2 test'), good.replace('1700000/1700000', '1699999/1700000')):
            with self.assertRaises(ValueError):
                engine_totals(bad, 0)


if __name__ == '__main__':
    unittest.main()
