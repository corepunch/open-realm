"""Reject omitted, stale, duplicated or unowned oracle limitations."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import audit_wc3_pathing_exclusion_owners as v


class ExclusionOwnerTests(unittest.TestCase):
    def setUp(self):
        self.corpus = json.loads(v.CORPUS.read_text())
        self.inventory = json.loads(v.OWNERS.read_text())
        self.todo = v.TODO.read_text()

    def validate(self):
        return v.validate(self.corpus, self.inventory, self.todo)

    def test_current_inventory(self):
        result = self.validate()
        self.assertEqual(result['unassigned'], 0)
        self.assertGreater(result['exclusions'], 0)
        self.assertEqual(sum(result['owners'].values()), result['exclusions'])

    def test_missing_assignment(self):
        self.inventory['entries'][0]['exclusions'].pop()
        with self.assertRaises(ValueError):
            self.validate()

    def test_changed_original_text(self):
        self.inventory['entries'][0]['exclusions'][0]['text'] += ' altered'
        with self.assertRaisesRegex(ValueError, 'exclusion changed'):
            self.validate()

    def test_duplicate_assignment(self):
        rows = self.inventory['entries'][0]['exclusions']
        rows.append(copy.deepcopy(rows[0]))
        with self.assertRaisesRegex(ValueError, 'duplicate exclusion'):
            self.validate()

    def test_new_exclusion_requires_assignment(self):
        entry = next(e for e in self.corpus['entries'] if e['kind'] == 'oracle' and e.get('exclusions') == [])
        entry['exclusions'].append('New unexplained branch')
        with self.assertRaisesRegex(ValueError, 'unassigned exclusions'):
            self.validate()

    def test_oracle_without_exclusions_still_in_inventory(self):
        self.corpus['entries'].append(dict(id='new-oracle', kind='oracle', exclusions=[]))
        with self.assertRaisesRegex(ValueError, 'oracle set changed'):
            self.validate()

    def test_nonprefixed_oracle_is_not_skipped(self):
        entry = next(e for e in self.inventory['entries'] if not e['oracle'].startswith('oracle-'))
        self.inventory['entries'].remove(entry)
        with self.assertRaisesRegex(ValueError, 'unassigned exclusions'):
            self.validate()

    def test_closed_unknown_and_self_owners_rejected(self):
        row = self.inventory['entries'][0]['exclusions'][0]
        for task in ('MISSING-99.9', 'BASE-03.3', 'E2E-02.3'):
            with self.subTest(task=task):
                row['task'] = task
                with self.assertRaisesRegex(ValueError, 'not a remaining task'):
                    self.validate()

    def test_owner_closure_requires_reassignment(self):
        task = self.inventory['entries'][0]['exclusions'][0]['task']
        self.todo = self.todo.replace('- [ ] **' + task + '**', '- [x] **' + task + '**')
        with self.assertRaisesRegex(ValueError, 'not a remaining task'):
            self.validate()

    def test_captures_cannot_replace_oracles(self):
        name = self.inventory['entries'][0]['oracle']
        next(e for e in self.corpus['entries'] if e['id'] == name)['kind'] = 'capture'
        with self.assertRaisesRegex(ValueError, 'oracle set changed'):
            self.validate()

    def test_rationale_required(self):
        self.inventory['entries'][0]['exclusions'][0]['rationale'] = ''
        with self.assertRaisesRegex(ValueError, 'missing ownership rationale'):
            self.validate()

    def test_unknown_disposition_rejected(self):
        self.inventory['entries'][0]['exclusions'][0]['disposition'] = 'proved'
        with self.assertRaisesRegex(ValueError, 'invalid disposition'):
            self.validate()


if __name__ == '__main__':
    unittest.main()
