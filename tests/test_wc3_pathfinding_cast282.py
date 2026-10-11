"""Keep original phase evidence literal and reject fixture weakening."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from verify_wc3_pathing_cast282 import SPEC, BUNDLE, normalized, validate_runtime


class Cast282Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(SPEC.read_text())
        cls.bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))

    def changed(self, event):
        bundle = copy.deepcopy(self.bundle)
        return bundle, next(r for r in bundle['captures'][0]['rows'] if r['event'] == event)

    def test_repeats_and_unhooked_control(self):
        self.assertEqual(validate_runtime(self.bundle, self.spec), dict(
            captures=2, controls=1, public_markers=1093, phase_cases=12, effect_stop_cases=2, effect_mana_cases=1))

    def test_one_clock_bit_is_not_tolerated(self):
        bundle, row = self.changed('effect-phase')
        row['clock'][0] ^= 1
        with self.assertRaisesRegex(ValueError, 'spell phase words'):
            validate_runtime(bundle, self.spec)

    def test_one_delay_bit_is_not_tolerated(self):
        bundle, row = self.changed('cast-phase')
        row['delays'][2] ^= 1
        with self.assertRaisesRegex(ValueError, 'spell phase words'):
            validate_runtime(bundle, self.spec)

    def test_scalar_dispatch_pointer_is_verified(self):
        bundle, row = self.changed('cast-phase')
        row['delays'][1] ^= 1
        with self.assertRaisesRegex(ValueError, 'scalar dispatch identity'):
            validate_runtime(bundle, self.spec)

    def test_concrete_method_is_not_discarded(self):
        bundle, row = self.changed('effect-body')
        row['methods'][2][1] ^= 1
        with self.assertRaisesRegex(ValueError, 'spell phase words'):
            validate_runtime(bundle, self.spec)

    def test_retired_effect_flags_cannot_be_changed(self):
        bundle, row = self.changed('effect-body')
        row['flags'] ^= 1
        with self.assertRaisesRegex(ValueError, 'spell phase words'):
            validate_runtime(bundle, self.spec)

    def test_caster_identity_is_checked_before_normalization(self):
        with self.assertRaisesRegex(ValueError, 'raw caster/trigger identity'):
            normalized(['C282 scene=0 label=context caster=12 trigger=13 target=0'])

    def test_target_identity_is_checked_before_normalization(self):
        with self.assertRaisesRegex(ValueError, 'raw target changed'):
            normalized(['C282 scene=3 label=context caster=12 trigger=12 target=13',
                        'C282 scene=3 label=context caster=12 trigger=12 target=14'])

    def test_control_cannot_have_hooks(self):
        bundle = copy.deepcopy(self.bundle)
        bundle['captures'][2]['rows'].insert(-1, dict(event='effect-body'))
        with self.assertRaisesRegex(ValueError, 'hooked control'):
            validate_runtime(bundle, self.spec)

    def test_incomplete_observer_is_rejected(self):
        bundle, row = self.changed('trace-end')
        row['counts']['effect-body'] -= 1
        with self.assertRaisesRegex(ValueError, 'observer incomplete'):
            validate_runtime(bundle, self.spec)

    def test_different_observer_source_is_rejected(self):
        bundle, row = self.changed('metadata')
        row['source_sha256']['cast282_observer.js'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'captured source'):
            validate_runtime(bundle, self.spec)

    def test_complete_phase_scheduling_is_still_open(self):
        self.assertIn('remain open', self.spec['scope'])
        rows = self.spec['contract']
        phases = {(r[0], r[1]): r[3] for r in rows if r[1] != 'public' and r[2] == 'begin'}
        self.assertEqual(phases[0, 'effect-phase']-phases[0, 'cast-phase'], 10)
        self.assertEqual(phases[3, 'cast-phase']-phases[3, 'channel-phase'], 7)


if __name__ == '__main__':
    unittest.main()
