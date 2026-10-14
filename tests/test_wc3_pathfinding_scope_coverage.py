import copy
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import audit_wc3_pathing_scope_coverage as audit


class ScopeCoverageTests(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads(audit.SPEC.read_text())
        bundle = json.loads(gzip.decompress(audit.BUNDLE.read_bytes()))
        self.static = json.loads(next(value for name, value in bundle['files'].items()
                                     if name.endswith('scope-exits-MAP-04.2.json')))
        self.verified = {name for row in self.spec['scopes'] for name in row['oracles']}
        tests = {name for row in self.spec['scopes'] for name in row['tests']}
        self.executed = {'classic': tests.copy(), 'tft': tests.copy()}

    def check(self):
        return audit.validate(self.spec, self.static, self.verified, self.executed)

    def test_engine_reports_include_sharded_and_unsharded_editions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            names = ['capscope273-classic.xml', 'map342-classic-0.xml',
                     'capscope273-tft.xml', 'map342-tft-0.xml', 'classic-not-a-report.xml']
            for name in names:
                (root / name).touch()
            self.assertEqual([p.name for p in audit.engine_reports(root, 'classic')],
                             ['capscope273-classic.xml', 'map342-classic-0.xml'])
            self.assertEqual([p.name for p in audit.engine_reports(root, 'tft')],
                             ['capscope273-tft.xml', 'map342-tft-0.xml'])

    def test_complete_inventory(self):
        result = self.check()
        self.assertEqual(result['static_scopes'], 12)
        self.assertEqual(result['reachable_scopes'], 11)

    def test_omitted_scope_rejected(self):
        self.spec['scopes'].pop()
        with self.assertRaises(ValueError): self.check()

    def test_duplicate_scope_rejected(self):
        self.spec['scopes'].append(copy.deepcopy(self.spec['scopes'][0]))
        with self.assertRaises(ValueError): self.check()

    def test_wrong_release_rejected(self):
        self.spec['scopes'][0]['releases'][0] = '0x0'
        with self.assertRaises(ValueError): self.check()

    def test_missing_native_oracle_rejected(self):
        self.verified.remove('oracle-removal304')
        with self.assertRaises(ValueError): self.check()

    def test_missing_edition_test_rejected(self):
        self.executed['tft'].remove('wc3_movement.scope300_idle_nested_pause')
        with self.assertRaises(ValueError): self.check()

    def test_omitted_restoration_assertion_rejected(self):
        self.spec['scopes'][0]['tests'] = []
        with self.assertRaises(ValueError): self.check()

    def test_unaccounted_exit_rejected(self):
        self.static['scopes'][0]['exits_while_held'].append('0x1234')
        with self.assertRaises(ValueError): self.check()


if __name__ == '__main__':
    unittest.main()
