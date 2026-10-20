import copy
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
sys.path.insert(0, str(ROOT / 'tools/frida/research'))
import verify_wc3_pathing_order_pool_growth as cold
import pool192_verify as live


class OrderPool(unittest.TestCase):
    def report(self):
        return json.loads((ROOT / 'tools/ghidra/fixtures/retail-order-pool-growth192-1.27.json').read_text())

    def rows(self):
        rows = [dict(event='marker', value='P192 tick=10 label=move')]
        for kind, size, block, count in [('task', 0x50, 64, 2), ('wrapper', 0xbc, 512, 5)]:
            rows.extend(dict(event='growth', kind=kind, size=size, block=block, grow=True,
                             bytes=size * block + 4, flags=0) for _ in range(count))
        for i in range(258):
            ident = i + 1
            rows.extend([dict(event='construct', id=ident, kind='task', refs=0,
                              identity=[0xffffffff] * 2, poolLive=i % 129 + 1),
                         dict(event='bind', id=ident, kind='task'),
                         dict(event='reclaim', id=ident, kind='task', refs=0, identity=[0xffffffff] * 2),
                         dict(event='wrapper-return', id=ident, kind='task', ownedNull=True, identity=[0xffffffff] * 2)])
        rows.append(dict(event='trace-end', livePayloads=0, liveWrappers=0,
                         counts=dict(construct=258, bind=258, reclaim=258, **{'wrapper-return': 258, 'marker': 270})))
        return rows

    def test_cold_both_classes_and_all_final_releases(self):
        cold.validate_report(self.report())

    def test_cold_rejects_missing_growth_release_and_reuse(self):
        for field, value in [('payload_growth_allocations', 0), ('wrapper_growth_allocations', 1),
                             ('reuse_allocations', 1), ('final_payload_live', 1), ('final_wrapper_live', 1)]:
            report = self.report(); report['classes'][0][field] = value
            with self.assertRaises(ValueError): cold.validate_report(report)
        report = self.report(); report['classes'][1]['phases'][1]['release_order'].pop()
        with self.assertRaises(ValueError): cold.validate_report(report)

    def test_complete_public_lifetimes(self):
        self.assertEqual(len(live.timeline(self.rows())), 1040)

    def test_public_rejects_missing_and_duplicate_boundary(self):
        for event in ('construct', 'bind', 'reclaim', 'wrapper-return'):
            rows = self.rows(); index = next(i for i, r in enumerate(rows) if r['event'] == event)
            rows.pop(index)
            with self.assertRaises(ValueError): live.timeline(rows)
            rows = self.rows(); rows.insert(index, copy.deepcopy(rows[index]))
            with self.assertRaises(ValueError): live.timeline(rows)

    def test_public_rejects_live_reference_and_wrapper_ownership(self):
        for event, field, value in [('reclaim', 'refs', 1), ('reclaim', 'identity', [0, 1]),
                                    ('wrapper-return', 'ownedNull', False), ('construct', 'poolLive', 0)]:
            rows = self.rows(); next(r for r in rows if r['event'] == event)[field] = value
            with self.assertRaises(ValueError): live.timeline(rows)

    def test_public_diagnostic_allocation_is_not_raw_growth(self):
        rows = self.rows(); rows.insert(0, dict(event='growth', kind='task', size=80,
                                               block=64, grow=False, bytes=19, flags=0))
        self.assertEqual(live.timeline(rows), live.timeline(self.rows()))

    def test_public_rejects_second_burst_growth_and_incomplete_finish(self):
        rows = self.rows(); rows.insert(1, dict(event='marker', value='P192 tick=30 label=move'))
        with self.assertRaises(ValueError): live.timeline(rows)
        rows = self.rows(); rows[-1]['livePayloads'] = 1
        with self.assertRaises(ValueError): live.timeline(rows)


if __name__ == '__main__': unittest.main()
