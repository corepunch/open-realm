"""Reject corrupted original structure-spell producers, motion and controls."""
import copy
import gzip
import json
import sys
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from verify_wc3_pathing_structure279 import SPEC, BUNDLE, validate_runtime

class Structure279Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(SPEC.read_text())
        cls.bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))

    def changed(self, event):
        bundle = copy.deepcopy(self.bundle)
        return bundle, next(row for row in bundle['captures'][0]['rows'] if row['event'] == event)

    def test_original_repeats_and_control(self):
        self.assertEqual(validate_runtime(self.bundle, self.spec), dict(
            captures=2, controls=1, public_markers=633, task_cases=6, physical_visits=471))

    def test_structure_cannot_use_persistent_follow(self):
        bundle, row = self.changed('begin-target')
        row['persistent'] = 1
        with self.assertRaisesRegex(ValueError, 'range/persistence'):
            validate_runtime(bundle, self.spec)

    def test_authored_range_cannot_be_substituted(self):
        bundle, row = self.changed('begin-task')
        row['range'] = 0x43960000
        with self.assertRaisesRegex(ValueError, 'range/persistence'):
            validate_runtime(bundle, self.spec)

    def test_physical_commit_cannot_change_one_bit(self):
        bundle, row = self.changed('gtick')
        row['members'][0]['pos'][0] ^= 1
        with self.assertRaisesRegex(ValueError, 'physical motion'):
            validate_runtime(bundle, self.spec)

    def test_public_command_cannot_be_move(self):
        bundle, row = self.changed('marker')
        row['value'] = row['value'].replace('label=start', 'label=changed')
        with self.assertRaisesRegex(ValueError, 'public markers'):
            validate_runtime(bundle, self.spec)

    def test_control_cannot_have_hooks(self):
        bundle = copy.deepcopy(self.bundle)
        bundle['captures'][2]['rows'].insert(-1, dict(event='trace-end'))
        with self.assertRaisesRegex(ValueError, 'hooked control'):
            validate_runtime(bundle, self.spec)

    def test_truncated_observer_is_rejected(self):
        bundle, row = self.changed('trace-end')
        row['counts']['gtick'] -= 1
        with self.assertRaisesRegex(ValueError, 'observer incomplete'):
            validate_runtime(bundle, self.spec)

    def test_captured_region_cannot_be_collision_rectangle(self):
        bundle = copy.deepcopy(self.bundle)
        row = next(r for r in bundle['detail_capture']['rows'] if r['event'] == 'fine-build-detail' and r['c'] == 1139)
        row['target']['box'][0] += 1
        with self.assertRaisesRegex(ValueError, 'blue target region'):
            validate_runtime(bundle, self.spec)

    def test_target_perimeter_cannot_be_blocked(self):
        bundle = copy.deepcopy(self.bundle)
        row = next(r for r in bundle['detail_capture']['rows'] if r['event'] == 'fine-cell-detail' and r['p'] == [46, 7])
        row['obstruction'] = 1
        with self.assertRaisesRegex(ValueError, 'target perimeter'):
            validate_runtime(bundle, self.spec)

    def test_route_initial_index_cannot_skip_destination(self):
        bundle = copy.deepcopy(self.bundle)
        rows = bundle['route_capture']['rows']
        row = next(r for r in rows if r['event'] == 'gtick' and r['c'] == 1140)
        row['members'][0]['path']['idx'][0] = 23
        with self.assertRaisesRegex(ValueError, 'partial route'):
            validate_runtime(bundle, self.spec)

if __name__ == '__main__':
    unittest.main()
