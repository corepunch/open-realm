"""The BASE-03.1 reachable inventory stays closed, linked and owned."""
import copy
import json
from pathlib import Path
import sys
import unittest

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
        self.assertFalse(self.validate()['closure_verified'])

    def test_function_only_inventory_cannot_close_leaf(self):
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


if __name__ == '__main__':
    unittest.main()
