"""Damaged lifecycle evidence cannot certify removal or a fresh request."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_blocker_lifecycle_trace import LABELS, STATE, REQUESTS, verify_lifecycle


class BlockerLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-blocker-lifecycle-1.27.json').read_text())
        self.rows = []
        requests = dict(zip(('trees_removed', 'mine_depleted', 'mine_removed', 'gate_removed'), REQUESTS))
        for label, state, life in zip(LABELS, STATE, self.fixture['life_markers'], strict=True):
            self.rows.append(dict(event='blocker-lifecycle-marker', value=life))
            self.rows.append(dict(event='blocker-geometry', marker=life,
                                  **copy.deepcopy(self.fixture['geometry'][state])))
            if label in requests:
                self.rows.append(dict(event='marker', value='PATHTRACE label=' + requests[label] + '_accepted x=0'))
                self.rows.append(dict(event='search', kind='fine'))
        for name in ('lumber', 'gold'):
            self.rows.append(dict(event='marker', value='PATHTRACE label=' + name + '_accepted x=0'))
        for i in range(7):
            self.rows.append(dict(event='widget-method', method='destroy', beforeCollection=hex(i+1), afterCollection='0x0'))
        self.rows.append(dict(event='marker', value='PATHTRACE label=complete x=0'))
        self.rows.append(dict(event='trace-end', installed=True, counts={'widget-destroy': 7}))

    def test_complete_lifecycle_contract(self):
        self.assertEqual(verify_lifecycle(self.rows, self.fixture),
                         dict(snapshots=13, fresh_requests=4, retired_collections=7))

    def test_stale_footprint_and_hierarchy_are_rejected(self):
        for key in ('masks', 'hierarchy'):
            rows = copy.deepcopy(self.rows)
            row = next(r for r in rows if r['event'] == 'blocker-geometry' and 'label=mine_depleted ' in r['marker'])
            row[key] = copy.deepcopy(self.fixture['geometry']['mine'][key])
            with self.assertRaisesRegex(ValueError, 'footprint or hierarchy'):
                verify_lifecycle(rows, self.fixture)

    def test_no_public_depletion_is_rejected(self):
        rows = [r for r in self.rows if not (r['event'] == 'blocker-lifecycle-marker' and 'label=tree_depleted_overlap ' in r['value'])]
        with self.assertRaisesRegex(ValueError, 'depletion, overlap'):
            verify_lifecycle(rows, self.fixture)

    def test_missing_next_fine_request_is_rejected(self):
        rows = copy.deepcopy(self.rows)
        next(r for r in rows if r['event'] == 'search')['kind'] = 'acc'
        with self.assertRaisesRegex(ValueError, 'fresh fine request'):
            verify_lifecycle(rows, self.fixture)

    def test_incomplete_native_free_is_rejected(self):
        rows = copy.deepcopy(self.rows)
        next(r for r in rows if r['event'] == 'widget-method')['afterCollection'] = '0x1234'
        with self.assertRaisesRegex(ValueError, 'collection retirement'):
            verify_lifecycle(rows, self.fixture)

    def test_failed_or_late_observer_is_rejected(self):
        for extra in (dict(event='trace-failed'), dict(event='blocker-geometry')):
            with self.assertRaisesRegex(ValueError, 'observer'):
                verify_lifecycle(self.rows + [extra], self.fixture)

    def test_scope_cannot_claim_whole_pathfinder(self):
        self.fixture['whole_retail_pathfinder'] = True
        with self.assertRaisesRegex(ValueError, 'scope'):
            verify_lifecycle(self.rows, self.fixture)


if __name__ == '__main__':
    unittest.main()
