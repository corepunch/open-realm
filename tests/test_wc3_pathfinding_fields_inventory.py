"""The BASE-03.2 field inventory stays complete, anchored and owned."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import audit_wc3_pathing_fields as v


class FieldInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = v.load()
        cls.types = json.loads(v.TYPES.read_text())
        cls.corpus = json.loads(v.inv.CORPUS.read_text())
        cls.todo = v.inv.TODO.read_text()
        cls.open = sorted(k for k, s in ((m[2], m[1]) for m in v.inv.TASK.finditer(cls.todo)) if s == ' ')[0]

    def setUp(self):
        self.inventory = copy.deepcopy(self.base)

    def validate(self):
        return v.validate(self.inventory, self.types, self.corpus, self.todo)

    def first(self, status, kind='fields'):
        return next(r for r in self.inventory[kind] if r['status'] == status)

    def test_current_inventory(self):
        result = self.validate()
        self.assertEqual(result['fields'], sum(len(l['fields']) for l in self.types['layouts']))
        self.assertGreater(sum(result['owners'].values()), 0)

    def test_missing_or_duplicate_field(self):
        self.inventory['fields'].pop()
        with self.assertRaisesRegex(ValueError, 'field set differs'):
            self.validate()
        self.inventory = copy.deepcopy(self.base)
        self.inventory['fields'].append(dict(self.inventory['fields'][0], id='B032-9999'))
        with self.assertRaisesRegex(ValueError, 'field set differs'):
            self.validate()

    def test_layout_change_requires_inventory_update(self):
        types = copy.deepcopy(self.types)
        types['layouts'][0]['fields'].append(dict(offset=4095, name='new_field', type='u32', evidence='x'))
        with self.assertRaisesRegex(ValueError, 'field set differs'):
            v.validate(self.inventory, types, self.corpus, self.todo)

    def test_owner_must_remain_open(self):
        for owner in ('BASE-03.2', 'BASE-03.3', None):
            self.inventory = copy.deepcopy(self.base)
            self.first('partial')['owner'] = owner
            with self.assertRaisesRegex(ValueError, 'remaining task'):
                self.validate()

    def test_covered_needs_accessors_and_anchor(self):
        row = self.first('covered')
        row['writers'] = []
        with self.assertRaisesRegex(ValueError, 'covered field lacks'):
            self.validate()
        self.inventory = copy.deepcopy(self.base)
        row = self.first('covered')
        row['evidence'] = [e for e in row['evidence'] if e['type'] == 'layout-evidence']
        with self.assertRaisesRegex(ValueError, 'covered field lacks'):
            self.validate()

    def test_stale_evidence(self):
        for item in (dict(type='payoff', id='Payoff99999'), dict(type='closed-task', id=self.open),
                     dict(type='oracle-exercising-accessor', id='oracle-missing'),
                     dict(type='doc-anchor-mentioning-accessor', anchor='docs/games/warcraft-3/retail-pathfinding.md#missing'),
                     dict(type='guess', id='x')):
            self.inventory = copy.deepcopy(self.base)
            self.first('covered')['evidence'].append(item)
            with self.assertRaises(ValueError):
                self.validate()

    def test_slot_global_and_gap_ownership(self):
        for kind in ('virtual_slots', 'globals', 'layout_gaps_accessed'):
            self.inventory = copy.deepcopy(self.base)
            row = next(r for r in self.inventory[kind] if r['status'] in ('partial', 'uncovered'))
            row['owner'] = 'BASE-03.3'
            with self.assertRaisesRegex(ValueError, 'remaining task'):
                self.validate()
        self.inventory = copy.deepcopy(self.base)
        self.inventory['layout_gaps_accessed'][0].update(status='covered', owner=None)
        with self.assertRaisesRegex(ValueError, 'prefix access'):
            self.validate()


if __name__ == '__main__':
    unittest.main()
