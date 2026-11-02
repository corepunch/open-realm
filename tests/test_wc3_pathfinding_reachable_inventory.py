"""Function-only and complete branch inventories have distinct acceptance gates."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import audit_wc3_pathing_reachable as v


class ReachableInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = v.load()
        cls.corpus = json.loads(v.inv.CORPUS.read_text())
        cls.todo = v.inv.TODO.read_text()
        cls.forced = json.loads(v.FORCED.read_text())

    def setUp(self):
        self.inventory = copy.deepcopy(self.base)

    def validate(self):
        return v.validate(self.inventory, self.corpus, self.todo, self.forced)

    def first(self, status):
        return next(r for r in self.inventory['functions'] if r['status'] == status)

    def test_current_inventory(self):
        result = self.validate()
        self.assertEqual(result['branches'], self.base['branch_totals']['branches'])
        self.assertGreater(result['guarded_branches'], 0)

    def test_function_totals_do_not_certify_branch_closure(self):
        self.todo = self.todo.replace('- [x] **BASE-03.1**', '- [ ] **BASE-03.1**')
        self.inventory.pop("branches", None)
        self.inventory.pop("branch_export", None)
        self.assertFalse(self.validate()['closure_verified'])

    def test_function_only_inventory_cannot_close_leaf(self):
        self.inventory.pop("branches", None)
        self.inventory.pop("branch_export", None)
        self.todo = self.todo.replace('- [ ] **BASE-03.1**', '- [x] **BASE-03.1**')
        try:
            with self.assertRaisesRegex(ValueError, 'individual branch records'):
                self.validate()
        finally:
            self.todo = v.inv.TODO.read_text()

    def test_duplicate_function(self):
        self.inventory['functions'].append(copy.deepcopy(self.inventory['functions'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate function'):
            self.validate()

    def test_covered_needs_guarding_oracle(self):
        row = self.first('covered')
        row['evidence'] = [e for e in row['evidence'] if e.get('tier') != 'executed-or-guarded']
        with self.assertRaisesRegex(ValueError, 'guarding oracle'):
            self.validate()

    def test_closed_owner_rejected(self):
        for kind in ('function', 'site'):
            self.inventory = copy.deepcopy(self.base)
            row = self.first('uncovered') if kind == 'function' else self.inventory['unresolved_sites'][0]
            row['owner'] = 'BASE-03.1'
            with self.assertRaisesRegex(ValueError, 'remaining task'):
                self.validate()

    def test_branch_totals_must_match(self):
        self.first('partial')['branches'] += 1
        with self.assertRaisesRegex(ValueError, 'branch totals'):
            self.validate()

    def test_forced_entry_must_be_root(self):
        self.inventory['undefined_roots'].clear()
        with self.assertRaisesRegex(ValueError, 'not a root'):
            self.validate()

    def test_complete_individual_branch_inventory(self):
        result = self.validate()
        self.assertTrue(result['closure_verified'])
        self.assertEqual(result['individual_branches'], 21306)

    def test_branch_deletion_rejected_even_with_adjusted_counts(self):
        row = self.inventory['branches'].pop(0)
        function = next(r for r in self.inventory['functions'] if r['address'] == row['function'])
        function['branches'] -= 1
        self.inventory['branch_totals']['branches'] -= 1
        with self.assertRaises(ValueError):
            self.validate()

    def test_duplicate_branch_rejected(self):
        self.inventory['branches'].append(copy.deepcopy(self.inventory['branches'][0]))
        with self.assertRaises(ValueError):
            self.validate()

    def test_branch_instruction_edges_and_function_match_export(self):
        for field, value in [('bytes','7500'), ('taken',['6f04e210']), ('fallthrough','6f04e210'),
                             ('mnemonic','JNZ'), ('function','6f04e550')]:
            self.inventory = copy.deepcopy(self.base)
            self.inventory['branches'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.validate()

    def test_branch_requires_correct_open_task_link(self):
        for owner in [None, 'BASE-03.1', 'TARGET-01.2-unknown', 'ORDER-01.16']:
            self.inventory['branches'][0]['owner'] = owner
            with self.subTest(owner=owner), self.assertRaises(ValueError):
                self.validate()

    def test_function_evidence_cannot_claim_branch_execution(self):
        self.inventory['branches'][0]['evidence'] = [{'kind':'oracle','id':'oracle-waygate-pool'}]
        with self.assertRaises(ValueError):
            self.validate()

    def test_previously_truncated_function_has_every_branch(self):
        branches = [r for r in self.inventory['branches'] if r['function'] == '6f4725f0']
        self.assertEqual(len(branches), 462)
        self.inventory['branches'].remove(branches[-1])
        with self.assertRaises(ValueError):
            self.validate()

    def test_export_identity_cannot_be_replaced(self):
        self.inventory['branch_export']['sha256'] = '0' * 64
        with self.assertRaises(ValueError):
            self.validate()

    def test_complete_branch_schema_can_certify_inventory_closure(self):
        self.todo = self.todo.replace('- [ ] **BASE-03.1**', '- [x] **BASE-03.1**')
        self.assertTrue(self.validate()['closure_verified'])
        self.assertFalse(self.validate()['runtime_branch_coverage_certified'])

    def test_export_payload_cannot_be_changed(self):
        raw = gzip.decompress((ROOT / v.BRANCH_EXPORT).read_bytes())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / v.BRANCH_EXPORT
            path.parent.mkdir(parents=True)
            path.write_bytes(gzip.compress(raw.replace(b'745e', b'755e', 1), mtime=0))
            with patch.object(v, 'ROOT', root), self.assertRaisesRegex(ValueError, 'original branch export differs'):
                self.validate()

    def test_generator_reproduces_reviewed_inventory(self):
        from generate_wc3_pathing_reachable_branches import build
        raw = gzip.decompress((ROOT / v.BRANCH_EXPORT).read_bytes())
        self.assertEqual(build(self.inventory, raw), self.inventory)
        with self.assertRaisesRegex(ValueError, 'fresh export differs'):
            build(self.inventory, raw + b'\n')


if __name__ == '__main__':
    unittest.main()
