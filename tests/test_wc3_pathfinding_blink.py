"""Blink evidence must preserve callback order, scope and public controls."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/ghidra/research'))
from verify_target182_blink import blink_contract


class BlinkEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.frozen = json.loads((ROOT/'tools/ghidra/fixtures/retail-target-visibility166-1.27.json').read_text())

    def test_public_blink_has_later_synchronous_retained_follow(self):
        result = blink_contract(self.frozen)
        self.assertEqual(result, dict(blink_events=1, receiver_events=1, retained_groups=2,
                                     notification_counter=7968, issue_counter=7957))

    def test_missing_window_receiver_or_retention_rejects_evidence(self):
        for change in ('window', 'receiver', 'validation', 'order', 'issue'):
            frozen = copy.deepcopy(self.frozen)
            blink = frozen['families']['loss'][10]
            if change == 'window': blink['target_lost'][0]['w20'] = '0x406'
            elif change == 'receiver': blink['handler'].clear()
            elif change == 'validation': blink['validate'][1]['result'] = '0xdd'
            elif change == 'order': blink['public_transitions'][2]['follower_order'] = '0'
            elif change == 'issue':
                next(m for m in blink['markers'] if m['label']=='end-produce')['c'] = 7968
            with self.subTest(change=change), self.assertRaises(ValueError): blink_contract(frozen)

    def test_original_matrix_keeps_nonunit_and_visibility_boundaries(self):
        original = json.loads((ROOT/'tools/ghidra/fixtures/retail-blink-validation182-1.27.json').read_text())
        self.assertEqual(original['cases'], 90)
        self.assertEqual(len(original['validation_cases']), 64)
        for row in original['validation_cases']:
            if not row['dead'] and row['unit'] and row['hidden'] and row['transient']:
                self.assertEqual(row['result'], 0 if row['visible'] else 0xdd)
                self.assertEqual(row['queries'], [['visibility', 0, 4]])
            if not row['dead'] and not row['unit'] and row['hidden']:
                self.assertEqual(row['result'], 0xaa)
                self.assertEqual(row['queries'], [])

    def test_original_dispatch_packet_and_clear_preserve_other_bits(self):
        original = json.loads((ROOT/'tools/ghidra/fixtures/retail-blink-validation182-1.27.json').read_text())
        for row in original['window_cases']:
            self.assertEqual([r[0] for r in row['trace']], ['prepare','relocate','notify','finish'])
            self.assertEqual(row['trace'][2][2:], [0xd01a4, 0x10000000, 0, 0])
            self.assertEqual(row['trace'][2][1], row['flags'] | 0x800000)
            before = row['flags'] | 0x800000 if row['event_flags'] is None else row['event_flags']
            self.assertEqual(row['final'], before & ~0x800000)


if __name__ == '__main__': unittest.main()
