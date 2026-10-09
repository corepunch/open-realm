import copy
import importlib.util
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida/research'))
from portal188_verify import portal_rows, verify

spec = importlib.util.spec_from_file_location('portal_scope', ROOT / 'tools/ghidra/verify_wc3_pathing_portal_scope.py')
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


class PortalScope(unittest.TestCase):
    def setUp(self):
        self.native = json.loads((ROOT / 'tools/ghidra/fixtures/retail-portal-scope188-1.27.json').read_text())
        self.live = json.loads((ROOT / 'tools/ghidra/fixtures/retail-portal-exclusion188-1.27.json').read_text())

    def raw(self):
        rows = copy.deepcopy(self.live['portal_rows'])
        for row in rows:
            row['self'] = '0x10000000'
            for key in ('callback', 'context', 'outer'):
                if key in row:
                    row[key] = '0x0'
        return rows

    def test_original_matrix(self):
        self.assertIs(oracle.validate_report(self.native), self.native)

    def test_missing_exit(self):
        self.native['cases'].pop()
        with self.assertRaises(ValueError):
            oracle.validate_report(self.native)

    def test_missing_or_leaked_hold(self):
        for key, value in [('held', 0), ('final_counter', 1)]:
            report = copy.deepcopy(self.native)
            report['cases'][0][key] = value
            with self.assertRaises(ValueError):
                oracle.validate_report(report)

    def test_native_outer_callback(self):
        self.native['cases'][0]['policy'][4] = 1
        with self.assertRaises(ValueError):
            oracle.validate_report(self.native)

    def test_live_normalization(self):
        self.assertEqual(portal_rows(self.raw()), self.live['portal_rows'])

    def test_live_unheld_callback(self):
        rows = self.raw()
        rows[2]['flags'] = 0
        with self.assertRaises(ValueError):
            portal_rows(rows)

    def test_live_counter_leak(self):
        rows = self.raw()
        rows[-1]['after'] = 1
        with self.assertRaises(ValueError):
            portal_rows(rows)

    def test_live_outer_callback(self):
        rows = self.raw()
        rows[2]['outer'] = '0x10004000'
        with self.assertRaises(ValueError):
            portal_rows(rows)

    def test_live_replaced_identity(self):
        rows = self.raw()
        rows[-1]['self'] = '0x10002000'
        with self.assertRaises(ValueError):
            portal_rows(rows)

    def test_live_missing_release(self):
        rows = self.raw()
        rows.pop()
        with self.assertRaises(ValueError):
            portal_rows(rows)


if __name__ == '__main__':
    unittest.main()
