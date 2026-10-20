"""Static inventories stay closed over their enumerated original sites."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import audit_wc3_pathing_inventory as v

INVENTORIES = sorted((ROOT / 'tools/ghidra/fixtures').glob('retail-*-inventory-1.27.json'))


class InventoryAuditTests(unittest.TestCase):
    def setUp(self):
        self.corpus = json.loads(v.CORPUS.read_text())
        self.todo = v.TODO.read_text()
        # Owners and stale references come from the live backlog so closing a leaf cannot invalidate the test.
        states = {m[2]: m[1] for m in v.TASK.finditer(self.todo)}
        self.open = sorted(k for k, s in states.items() if s == ' ' and k != 'BASE-01.1')[0]
        self.inventory = dict(version=1, task='BASE-01.1', prefix='T-', game_sha256=v.SHA,
            enumeration=[dict(id='Q1', query='direct callers', sites=['6f21560a', '6f4c961c', '6f54db2f'])],
            rows=[dict(id='T-01', producer='JASS SetUnitPosition', sites=['6f21560a'], status='covered',
                       evidence=['oracle:oracle-stop285', 'doc:retail-pathfinding-engine.md#forced-position-stop-reaches-the-engine',
                                 'todo:BASE-01.6'], engine='implemented', owner=None, rationale='Public Stop and placement.'),
                  dict(id='T-02', producer='Blink', sites=['6f4c961c'], status='partial', evidence=[],
                       engine='partial', owner=self.open, rationale='Cast lifetime remains.')],
            excluded=[dict(site='6f54db2f', reason='Different receiver class.')], guards={})

    def validate(self, inventory=None):
        return v.validate(inventory or self.inventory, self.corpus, self.todo)

    def test_synthetic_inventory(self):
        result = self.validate()
        self.assertEqual((result['rows'], result['enumerated_sites'], result['excluded']), (2, 3, 1))
        self.assertEqual(result['owners'], {self.open: 1})

    def test_unowned_site(self):
        self.inventory['excluded'].clear()
        with self.assertRaisesRegex(ValueError, 'unowned enumerated sites'):
            self.validate()

    def test_site_rows_and_exclusion_conflict(self):
        self.inventory['rows'][1]['sites'].append('6f21560a')
        self.validate()
        self.inventory['rows'][1]['sites'].append('6f54db2f')
        with self.assertRaisesRegex(ValueError, 'also owned by a row'):
            self.validate()

    def test_unenumerated_site(self):
        self.inventory['rows'][1]['sites'].append('6f000000')
        with self.assertRaisesRegex(ValueError, 'not enumerated'):
            self.validate()

    def test_closed_or_self_owner(self):
        for owner in ('BASE-01.6', 'BASE-01.1', 'NOPE-01.1'):
            self.inventory['rows'][1]['owner'] = owner
            with self.assertRaisesRegex(ValueError, 'remaining task'):
                self.validate()

    def test_covered_needs_evidence(self):
        self.inventory['rows'][0]['evidence'] = []
        with self.assertRaisesRegex(ValueError, 'covered row'):
            self.validate()

    def test_stale_references(self):
        for ref in ('oracle:oracle-missing', 'doc:retail-pathfinding-engine.md#no-such-heading', 'doc:missing.md',
                    'todo:' + self.open, 'payoff:285'):
            changed = copy.deepcopy(self.inventory)
            changed['rows'][0]['evidence'] = [ref]
            with self.assertRaises(ValueError):
                self.validate(changed)

    def test_unreachable_needs_proof(self):
        self.inventory['rows'][1].update(status='unreachable', owner=None)
        with self.assertRaisesRegex(ValueError, 'unproved unreachable'):
            self.validate()
        self.inventory['rows'][1]['proof'] = 'No direct or vtable reference reaches the body.'
        self.validate()

    def test_guard_bytes_follow_pe_sections(self):
        # Minimal PE: image base 6f000000, one .text section mapping RVA1000 at file offset200.
        data = bytearray(0x400)
        data[0x3c:0x40] = (0x80).to_bytes(4, 'little')
        data[0x86:0x88] = (1).to_bytes(2, 'little')
        data[0x94:0x96] = (0xe0).to_bytes(2, 'little')
        data[0x98 + 28:0x98 + 32] = (0x6f000000).to_bytes(4, 'little')
        section = 0x98 + 0xe0
        for offset, value in ((8, 0x100), (12, 0x1000), (16, 0x100), (20, 0x200)):
            data[section + offset:section + offset + 4] = value.to_bytes(4, 'little')
        data[0x210:0x216] = bytes.fromhex('ff9080010000')
        self.assertEqual(v.guard_bytes(bytes(data), {'6f001010': 'ff9080010000'}), 1)
        for guards in ({'6f001010': 'ff9080010001'}, {'6f002000': 'ff'}):
            with self.assertRaisesRegex(ValueError, 'instruction differs'):
                v.guard_bytes(bytes(data), guards)

    def test_published_inventories(self):
        for path in INVENTORIES:
            with self.subTest(path=path.name):
                result = self.validate(json.loads(path.read_text()))
                self.assertEqual(result['status'], 'verified')


if __name__ == '__main__':
    unittest.main()
