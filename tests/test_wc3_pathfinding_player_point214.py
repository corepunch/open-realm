"""Player transport contracts must retain the literal original fields and ABIs."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_player_point214 as V


class PlayerPointContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(V.FIXTURE.read_text())
        cls.bundle = json.loads(gzip.decompress(V.BUNDLE.read_bytes()))

    def test_original_transport_and_admission_inventory(self):
        self.assertEqual(V.validate_runtime(self.bundle, V.validate(self.spec)),
                         dict(captures=2, controls=1, actions=6, publications=36, replacements=24))

    def test_existing_motion_expectations_are_pinned(self):
        for path in V.SOURCES:
            spec = copy.deepcopy(self.spec)
            spec['pins'][path] = '0'*64
            with self.assertRaises(ValueError):
                V.validate(spec)

    def test_every_transport_stage_preserves_fractional_words_and_flags(self):
        for stage in ('ui-point', 'submit', 'serialize', 'deserialize', 'action'):
            for field in ('point', 'flags', 'order'):
                bundle = copy.deepcopy(self.bundle)
                row = next(r for r in bundle['captures'][0]['rows'] if r['event'] == stage)
                if field == 'point':
                    row[field][0] ^= 1
                else:
                    row[field] ^= 1
                with self.assertRaises(ValueError):
                    V.validate_runtime(bundle, self.spec)

    def test_decoded_dispatcher_player_and_identity_fields(self):
        for field, value in [('entry', '6b98f0'), ('player', 0), ('caller', '32d661')]:
            bundle = copy.deepcopy(self.bundle)
            next(r for r in bundle['captures'][0]['rows'] if r['event'] == 'action')[field] = value
            with self.assertRaises(ValueError):
                V.validate_runtime(bundle, self.spec)
        for offset in (2, 5, 8, 12):
            bundle = copy.deepcopy(self.bundle)
            next(r for r in bundle['captures'][0]['rows'] if r['event'] == 'action')['words'][offset] ^= 1
            with self.assertRaises(ValueError):
                V.validate_runtime(bundle, self.spec)

    def test_published_payload_cannot_be_truncated_or_rebound(self):
        for offset in (9, 10, 18, 20):
            bundle = copy.deepcopy(self.bundle)
            next(r for r in bundle['captures'][0]['rows'] if r['event'] == 'publish')['orderWords'][offset] ^= 1
            with self.assertRaises(ValueError):
                V.validate_runtime(bundle, self.spec)

    def test_replacement_stack_abi_and_unit_identity(self):
        for field, value in [('mode', 0), ('dispatch', 0), ('caller', '6b9530'), ('unit', '0x0'), ('order', '0x0')]:
            bundle = copy.deepcopy(self.bundle)
            rows = bundle['captures'][0]['rows']
            first = next(r['seq'] for r in rows if r['event'] == 'action')
            next(r for r in rows if r['event'] == 'admit' and r['seq'] > first)[field] = value
            with self.assertRaises(ValueError):
                V.validate_runtime(bundle, self.spec)

    def test_partial_observer_and_instrumented_control_are_rejected(self):
        for index in range(2):
            bundle = copy.deepcopy(self.bundle)
            next(r for r in bundle['captures'][index]['rows'] if r['event'] == 'trace-end')['readOnly'] = False
            with self.assertRaises(ValueError):
                V.validate_runtime(bundle, self.spec)
        bundle = copy.deepcopy(self.bundle)
        bundle['captures'][2]['rows'].insert(1, dict(event='module'))
        with self.assertRaises(ValueError):
            V.validate_runtime(bundle, self.spec)

    def test_repeats_and_control_are_required(self):
        for index in range(3):
            bundle = copy.deepcopy(self.bundle)
            bundle['captures'].pop(index)
            with self.assertRaises(ValueError):
                V.validate_runtime(bundle, self.spec)

    def test_source_and_helper_provenance_are_required(self):
        for name in ('point214_observer.js', 'point214_ui_input.exe.so', 'map'):
            bundle = copy.deepcopy(self.bundle)
            bundle['captures'][0]['rows'][0]['source_sha256'][name] = '0'*64
            with self.assertRaises(ValueError):
                V.validate_runtime(bundle, self.spec)

    def test_partial_native_input_and_public_output_are_rejected(self):
        bundle = copy.deepcopy(self.bundle)
        next(r for r in bundle['captures'][0]['rows'] if r['event'] == 'player-input')['rc'] = 1
        with self.assertRaises(ValueError):
            V.validate_runtime(bundle, self.spec)
        bundle = copy.deepcopy(self.bundle)
        bundle['captures'][2]['preload'] = ''
        with self.assertRaises(ValueError):
            V.validate_runtime(bundle, self.spec)


if __name__ == '__main__':
    unittest.main()
