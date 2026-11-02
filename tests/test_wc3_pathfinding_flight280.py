"""Reject corrupted native flying spell ownership, motion and capture provenance."""
import copy
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from verify_wc3_pathing_flight280 import SPEC, BUNDLE, validate_runtime, validate_fixture


class Flight280Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(SPEC.read_text())
        cls.bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))

    def changed(self, event):
        bundle = copy.deepcopy(self.bundle)
        return bundle, next(r for r in bundle['captures'][0]['rows'] if r['event'] == event)

    def test_original_repeats_control_and_engine_fixture(self):
        self.assertEqual(validate_runtime(self.bundle, self.spec), dict(
            captures=2, controls=1, public_markers=634, task_cases=6, physical_visits=357))
        validate_fixture(ROOT / 'games/warcraft-3/game/skills/fixtures/retail_flight280_motion.h', self.spec)

    def test_target_cannot_become_persistent_follow(self):
        bundle, row = self.changed('begin-target')
        row['persistent'] = 1
        with self.assertRaisesRegex(ValueError, 'range/persistence'):
            validate_runtime(bundle, self.spec)

    def test_point_range_cannot_include_collision(self):
        bundle = copy.deepcopy(self.bundle)
        row = next(r for r in bundle['captures'][0]['rows']
                   if r['event'] == 'begin-point' and r['range'] != 0)
        row['range'] += 1
        with self.assertRaisesRegex(ValueError, 'point captured range'):
            validate_runtime(bundle, self.spec)

    def test_committed_heading_cannot_change_one_bit(self):
        bundle, row = self.changed('gtick')
        row['members'][0]['facing'] ^= 1
        with self.assertRaisesRegex(ValueError, 'physical motion'):
            validate_runtime(bundle, self.spec)

    def test_velocity_cannot_change_one_bit(self):
        bundle, row = self.changed('gtick')
        row['members'][0]['vel'][0] ^= 1
        with self.assertRaisesRegex(ValueError, 'physical motion'):
            validate_runtime(bundle, self.spec)

    def test_superseded_group_cannot_own_the_new_task(self):
        bundle = copy.deepcopy(self.bundle)
        row = next(r for r in bundle['captures'][0]['rows'] if r['event'] == 'gtick'
                   and r['members'] and r['members'][0].get('grp') != r['id'])
        row['members'][0]['grp'] = row['id']
        with self.assertRaisesRegex(ValueError, 'physical ownership count'):
            validate_runtime(bundle, self.spec)

    def test_new_flyer_cannot_bypass_adaptive_routing(self):
        bundle, row = self.changed('fine')
        row['cnt'] = [0, 0]
        with self.assertRaisesRegex(ValueError, 'adaptive route'):
            validate_runtime(bundle, self.spec)

    def test_control_cannot_have_hooks(self):
        bundle = copy.deepcopy(self.bundle)
        bundle['captures'][2]['rows'].insert(-1, dict(event='trace-end'))
        with self.assertRaisesRegex(ValueError, 'hooked control'):
            validate_runtime(bundle, self.spec)

    def test_incomplete_observer_is_rejected(self):
        bundle, row = self.changed('trace-end')
        row['counts']['gtick'] -= 1
        with self.assertRaisesRegex(ValueError, 'observer incomplete'):
            validate_runtime(bundle, self.spec)

    def test_different_observer_source_is_rejected(self):
        bundle, row = self.changed('metadata')
        row['source_sha256']['flight280_observer.js'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'captured source'):
            validate_runtime(bundle, self.spec)

    def test_engine_literal_cannot_replace_the_native_expectation(self):
        path = ROOT / 'games/warcraft-3/game/skills/fixtures/retail_flight280_motion.h'
        with tempfile.TemporaryDirectory() as directory:
            changed = Path(directory) / 'motion.h'
            changed.write_text(path.read_text().replace('0x413a0000', '0x413a0001', 1))
            with self.assertRaisesRegex(ValueError, 'engine fixture'):
                validate_fixture(changed, self.spec)


if __name__ == '__main__':
    unittest.main()
