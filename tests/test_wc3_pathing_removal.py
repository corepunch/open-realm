"""Removal evidence must retain enabled controls and every observable boundary."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida/research'))
from removal204_verify import FIXTURE, claims, stages, validate


class RemovalEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads(gzip.decompress(FIXTURE.read_bytes()))

    def test_complete_enabled_repeats_and_observer_free_control(self):
        report = validate(self.spec)
        self.assertEqual((report['observations'], report['controls'], report['native_stages']), (2, 1, 286))

    def test_pending_removal_cannot_recreate_separation(self):
        for event in ('remove-end', 'acquire-end', 'marker'):
            sequence = copy.deepcopy(self.spec['stages'])
            for row in sequence:
                if row['event'] == event and (event != 'marker' or 'label=nested_owner ' in row['value']):
                    unit = row['unit'] if event != 'marker' else row['units'][0]
                    unit['sep'] = 0x11110000
                    break
            with self.assertRaises(ValueError):
                claims(sequence)

    def test_disabled_only_controls_and_missing_retirement_are_rejected(self):
        sequence = copy.deepcopy(self.spec['stages'])
        for row in sequence:
            if row['event'] == 'remove-begin':
                row['unit']['sep'] = None
        with self.assertRaises(ValueError):
            claims(sequence)
        with self.assertRaises(ValueError):
            claims([r for r in self.spec['stages'] if r['event'] != 'destroy'])

    def test_truncation_stale_raw_pins_and_missing_control_are_rejected(self):
        for change in ('rows', 'sha256', 'control'):
            spec = copy.deepcopy(self.spec)
            if change == 'rows':
                spec['captures'][0]['rows'].pop()
            elif change == 'sha256':
                spec['captures'][0]['sha256'] = '0' * 64
            else:
                spec['captures'].pop()
            with self.assertRaises(ValueError):
                validate(spec)

    def test_normalization_preserves_depth_policy_and_raw_input(self):
        rows = self.spec['captures'][0]['rows']
        original = copy.deepcopy(rows)
        self.assertEqual(stages(rows), self.spec['stages'])
        self.assertEqual(rows, original)
        row = next(r for r in rows if r['event'] == 'remove-end')
        altered = copy.deepcopy(rows)
        next(r for r in altered if r['event'] == 'remove-end')['unit']['depth'] = 0
        self.assertNotEqual(stages(altered), stages(rows))
        self.assertEqual(row['unit']['depth'], 1)


if __name__ == '__main__':
    unittest.main()
