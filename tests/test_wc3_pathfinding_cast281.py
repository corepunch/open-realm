"""Protect literal retail point CHANNEL evidence and its documented limits."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from verify_wc3_pathing_cast281 import SPEC, BUNDLE, normalized, validate_runtime


class Cast281Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(SPEC.read_text())
        cls.bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))

    def changed(self, event):
        bundle = copy.deepcopy(self.bundle)
        return bundle, next(r for r in bundle['captures'][0]['rows'] if r['event'] == event)

    def test_original_repeats_and_control(self):
        self.assertEqual(validate_runtime(self.bundle, self.spec), dict(
            captures=2, controls=1, public_markers=715, channel_cases=8, cancellation_cases=6))

    def test_clock_cannot_change_one_bit(self):
        bundle, row = self.changed('cast-begin')
        row['clock'][0] ^= 1
        with self.assertRaisesRegex(ValueError, 'channel/cast words'):
            validate_runtime(bundle, self.spec)

    def test_callback_cannot_retain_cancelled_cast_flags(self):
        bundle = copy.deepcopy(self.bundle)
        row = next(r for r in bundle['captures'][0]['rows']
                   if r['event'] == 'cast-begin' and r.get('flags') == 16)
        row['flags'] = 38933
        with self.assertRaisesRegex(ValueError, 'channel/cast words'):
            validate_runtime(bundle, self.spec)

    def test_channel_and_cast_must_refer_to_same_native_ability(self):
        bundle, row = self.changed('channel-producer')
        row['ability'] = '0x0'
        with self.assertRaisesRegex(ValueError, 'cast ownership'):
            validate_runtime(bundle, self.spec)

    def test_caster_handles_are_relational_before_normalization(self):
        with self.assertRaisesRegex(ValueError, 'raw caster/trigger identity'):
            normalized(['C281 player-context caster=123 trigger=124'])
        self.assertEqual(normalized(['C281 player-context caster=123 trigger=123']),
                         ['C281 player-context caster=CASTER trigger=CASTER'])

    def test_identity_normalization_cannot_hide_point_or_mana(self):
        self.assertEqual(normalized(['C281 player-context caster=123 trigger=123 point=1536,288 mana=200']),
                         ['C281 player-context caster=CASTER trigger=CASTER point=1536,288 mana=200'])

    def test_control_cannot_have_hooks(self):
        bundle = copy.deepcopy(self.bundle)
        bundle['captures'][2]['rows'].insert(-1, dict(event='channel-producer'))
        with self.assertRaisesRegex(ValueError, 'hooked control'):
            validate_runtime(bundle, self.spec)

    def test_incomplete_observer_is_rejected(self):
        bundle, row = self.changed('trace-end')
        row['counts']['cast-begin'] -= 1
        with self.assertRaisesRegex(ValueError, 'observer incomplete'):
            validate_runtime(bundle, self.spec)

    def test_different_observer_source_is_rejected(self):
        bundle, row = self.changed('metadata')
        row['source_sha256']['cast281_observer.js'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'captured source'):
            validate_runtime(bundle, self.spec)

    def test_native_cast_delay_is_retained_as_unported_evidence(self):
        rows = self.bundle['captures'][0]['rows']
        channels = [r['c'] for r in rows if r['event'] == 'cast-begin' and r['phase'] == 'begin']
        effects = [r['c'] for r in rows if r['event'] == 'marker' and ' label=effect ' in r['value']]
        self.assertEqual(effects, [channels[0]+10, channels[4]+10])
        self.assertIn('remain open', self.spec['scope'])


if __name__ == '__main__':
    unittest.main()
