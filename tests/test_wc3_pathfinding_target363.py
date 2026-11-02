#!/usr/bin/env python3
"""Reject incomplete or altered mixed-owner retail event evidence."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_target363 as verifier


class MixedTargetEvents(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(verifier.SPEC.read_text())
        cls.groups = verifier.validate(cls.spec)

    def reject(self, change):
        groups = copy.deepcopy(self.groups)
        change(groups)
        with self.assertRaises((ValueError, AssertionError, StopIteration)):
            verifier.runtime(groups, self.spec)

    def test_all_public_mutations_have_independent_repeat_and_control(self):
        self.assertEqual(verifier.runtime(self.groups, self.spec), dict(
            observed_captures=6, controls=3, public_markers=2718, native_records=438))

    def test_missing_repeat_is_rejected(self):
        self.reject(lambda groups: groups['owner'].pop(1))

    def test_missing_public_mutation_is_rejected(self):
        self.reject(lambda groups: groups.pop('death'))

    def test_observer_free_control_cannot_contain_hooks(self):
        self.reject(lambda groups: groups['remove'][2]['rows'].insert(1, {'event': 'module'}))

    def test_incomplete_capture_is_rejected(self):
        self.reject(lambda groups: groups['owner'][0]['rows'][-1].__setitem__('complete', False))

    def test_preload_must_match_its_recorded_digest(self):
        self.reject(lambda groups: groups['death'][2].__setitem__(
            'preload', groups['death'][2]['preload'] + '\n'))

    def test_capture_provenance_is_bound_to_actual_map(self):
        self.reject(lambda groups: groups['remove'][0]['rows'][0]['source_sha256'].__setitem__('map', '0' * 64))

    def test_all_synchronous_callback_records_are_retained(self):
        def change(groups):
            rows = groups['remove'][0]['rows']
            start = next(i for i, row in enumerate(rows) if row.get('event') == 'marker' and 'label=mixed-remove-before' in row['value'])
            victim = next(i for i, row in enumerate(rows[start:], start) if row.get('event') == 'registry-callback-leave')
            del rows[victim]
        self.reject(change)

    def test_mixed_callback_order_cannot_be_family_sorted(self):
        def change(groups):
            rows = groups['owner'][0]['rows']
            row = next(row for row in rows if row.get('event') == 'registry-dispatch-enter' and row.get('code') == 0xd01a2 and len(row['nodes']) > 9)
            row['nodes'][-1], row['nodes'][-2] = row['nodes'][-2], row['nodes'][-1]
        self.reject(change)

    def test_scalar_captain_state_cannot_be_normalized_away(self):
        def change(groups):
            rows = groups['death'][0]['rows']
            start = next(i for i, row in enumerate(rows) if row.get('event') == 'marker' and 'label=mixed-death-before' in row['value'])
            def mutate(value):
                if isinstance(value, dict):
                    if 'current' in value:
                        value['current'][2] += 1
                        return True
                    return any(mutate(item) for item in value.values())
                if isinstance(value, list):
                    return any(mutate(item) for item in value)
                return False
            self.assertTrue(any(mutate(row) for row in rows[start:]))
        self.reject(change)

    def test_incomplete_original_instruction_inventory_is_rejected(self):
        spec = copy.deepcopy(self.spec)
        spec['instructions'].pop(next(iter(spec['instructions'])))
        with self.assertRaises(ValueError):
            verifier.validate(spec)


if __name__ == '__main__':
    unittest.main()
