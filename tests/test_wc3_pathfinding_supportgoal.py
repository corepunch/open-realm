"""Preserved support/widget observations stay complete and linked to their inputs."""
import copy
import base64
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_supportgoal as v


class SupportGoalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(v.SPEC.read_text())
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
        cls.recovery = json.loads(gzip.decompress((ROOT / v.RECOVERY_INPUTS).read_bytes()))

    def water(self):
        return json.loads(gzip.decompress((ROOT / v.WATER_INPUTS).read_bytes()))

    def test_water_grid_rejects_changed_repeat_with_valid_raw_digest(self):
        import hashlib
        bundle = self.water()
        case = bundle['cases']['b']
        rows = [json.loads(line) for line in base64.b64decode(case['raw']).splitlines()]
        next(row for row in rows if row.get('event') == 'water-support-grid')['values'][0] ^= 1
        raw = ('\n'.join(json.dumps(row) for row in rows) + '\n').encode()
        case['raw'] = base64.b64encode(raw).decode()
        case['sha256'] = hashlib.sha256(raw).hexdigest()
        with self.assertRaisesRegex(ValueError, 'repeated words differ'):
            v.validate_water(self.spec['prepared_water'], self.bundle['widget'], bundle)

    def test_water_grid_requires_two_bounded_observations(self):
        bundle = self.water()
        bundle['cases'].pop('b')
        with self.assertRaisesRegex(ValueError, 'repeat missing'):
            v.validate_water(self.spec['prepared_water'], self.bundle['widget'], bundle)

    def test_bounded_water_diagnostics_cannot_claim_journey_acceptance(self):
        spec = dict(self.spec['prepared_water'], journey_acceptance=True)
        with self.assertRaisesRegex(ValueError, 'diagnostic scope differs'):
            v.validate_water(spec, self.bundle['widget'])

    def ramp(self):
        return json.loads(gzip.decompress((ROOT / v.RAMP_INPUTS).read_bytes()))

    def test_ramp_requires_both_producer_observations(self):
        bundle = self.ramp();bundle['cases'].pop('b')
        with self.assertRaisesRegex(ValueError, 'producer repeat missing'):
            v.validate_ramp(self.spec['ramp'], bundle)

    def test_ramp_rejects_altered_corner_with_valid_raw_digest(self):
        import hashlib
        bundle = self.ramp();case = bundle['cases']['b']
        rows = [json.loads(line) for line in base64.b64decode(case['raw']).splitlines()]
        next(row for row in rows if row.get('event') == 'ramp-offset')['value'] ^= 1
        raw = ('\n'.join(json.dumps(row) for row in rows) + '\n').encode()
        case['raw'] = base64.b64encode(raw).decode();case['sha256'] = hashlib.sha256(raw).hexdigest()
        with self.assertRaisesRegex(ValueError, 'producer repeats differ'):
            v.validate_ramp(self.spec['ramp'], bundle)

    def test_ramp_rejects_lost_quarter_outcome(self):
        data = json.loads(gzip.decompress((ROOT / v.RAMP_KERNELS).read_bytes()))
        next(row for row in data['cases'] if row['offset'] == 0x42000000)['offset'] = 0
        with self.assertRaisesRegex(ValueError, 'corner outcomes differ'):
            v.validate_ramp(self.spec['ramp'], kernels=data)

    def test_ramp_rejects_changed_authored_cap_field(self):
        data = json.loads(gzip.decompress((ROOT / v.RAMP_KERNELS).read_bytes()))
        data['prepared_grids'][0]['values'][0] ^= 1
        with self.assertRaisesRegex(ValueError, 'field/literal header differs'):
            v.validate_ramp(self.spec['ramp'], kernels=data)

    def metadata(self):
        return json.loads(gzip.decompress((ROOT / v.METADATA_INPUTS).read_bytes()))

    def test_metadata_rejects_wrapping_integer_instead_of_sse_indefinite(self):
        data = self.metadata()
        next(row for row in data['cases'] if row['text']=='1e400')['cap'] = 0
        with self.assertRaisesRegex(ValueError, 'producer/literal header differs'):
            v.validate_metadata(self.spec['metadata'], data)

    def test_metadata_requires_actual_ini_field_cases(self):
        data = self.metadata();data['fields'].pop(0)
        with self.assertRaisesRegex(ValueError, 'producer/literal header differs'):
            v.validate_metadata(self.spec['metadata'], data)

    def test_metadata_storm_identity_is_exact(self):
        data = self.metadata();data['storm_sha256'] = '0'*64
        with self.assertRaisesRegex(ValueError, 'producer/literal header differs'):
            v.validate_metadata(self.spec['metadata'], data)

    def widget(self):
        return [dict(cap, rows=list(cap['rows'])) for cap in self.bundle['widget']]

    def test_preserved_complete_contract(self):
        result = v.validate(self.spec, self.bundle)
        self.assertEqual(result['support']['scenes'], 200)
        self.assertEqual(result['widget']['successor_binds'], 36)

    def test_missing_engine_pattern_rejected(self):
        spec = copy.deepcopy(self.spec)
        spec['engine_tests'].remove('wc3_movement.widgetgoal*')
        with self.assertRaises(ValueError):
            v.validate(spec, self.bundle)

    def test_missing_guard_rejected_even_with_adjusted_total(self):
        spec = copy.deepcopy(self.spec)
        spec['instructions'].pop(next(iter(spec['instructions'])))
        spec['instruction_count'] -= 1
        with self.assertRaises(ValueError):
            v.validate(spec, self.bundle)

    def test_missing_repeat_rejected(self):
        with self.assertRaises(ValueError):
            v.validate_family('widget', self.spec, self.widget()[:2])

    def test_lost_native_event_rejected(self):
        caps = self.widget()
        caps[0]['rows'].pop(next(i for i,r in enumerate(caps[0]['rows']) if r.get('event') == 'commit-enter'))
        with self.assertRaisesRegex(ValueError, 'lost native observer events'):
            v.validate_family('widget', self.spec, caps)

    def test_control_cannot_contain_native_hooks(self):
        caps = self.widget()
        caps[2]['rows'].insert(-1, next(r for r in caps[0]['rows'] if r.get('event') == 'commit-enter'))
        with self.assertRaisesRegex(ValueError, 'hooked control'):
            v.validate_family('widget', self.spec, caps)

    def test_preload_newline_conversion_rejected(self):
        caps = self.widget()
        self.assertIn('\r\n', caps[0]['preload'])
        caps[0]['preload'] = caps[0]['preload'].replace('\r\n', '\n')
        with self.assertRaisesRegex(ValueError, 'capture provenance'):
            v.validate_family('widget', self.spec, caps)

    def test_environment_display_pair_must_match(self):
        caps = self.widget()
        caps[0]['rows'][0] = dict(caps[0]['rows'][0], display=':99')
        self.assertEqual(caps[0]['rows'][0]['env'], 'B')
        with self.assertRaisesRegex(ValueError, 'capture provenance'):
            v.validate_family('widget', self.spec, caps)

    def test_recovery_requires_two_observations(self):
        bundle = copy.deepcopy(self.recovery)
        bundle['cases'].pop('observe_b')
        with self.assertRaisesRegex(ValueError, 'missing recovery repeat'):
            v.validate_recovery(self.spec['recovery'], self.bundle['widget'], bundle)

    def test_recovery_arguments_are_not_discarded_with_pointer_normalization(self):
        for field in ['mask','context','callback']:
            bundle = copy.deepcopy(self.recovery)
            case = bundle['cases']['observe_a']
            rows = [json.loads(line) for line in base64.b64decode(case['capture']).splitlines()]
            row = next(r for r in rows if r.get('event') == 'recover-enter')
            if field == 'context':
                row[field][0] += 1
            elif field == 'mask':
                row[field] ^= 2
            else:
                row[field] = '0x654061'
            case['capture'] = base64.b64encode(('\n'.join(json.dumps(r) for r in rows)+'\n').encode()).decode()
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'arguments/support level'):
                v.validate_recovery(self.spec['recovery'], self.bundle['widget'], bundle)

    def test_recovery_core_must_equal_original_complete_capture(self):
        bundle = copy.deepcopy(self.recovery)
        case = bundle['cases']['observe_a']
        rows = [json.loads(line) for line in base64.b64decode(case['capture']).splitlines()]
        row = next(r for r in rows if r.get('event') == 'commit-leave')
        row['g']['members'][0]['m']['pos'][0] ^= 1
        case['capture'] = base64.b64encode(('\n'.join(json.dumps(r) for r in rows)+'\n').encode()).decode()
        with self.assertRaisesRegex(ValueError, 'core stream differs'):
            v.validate_recovery(self.spec['recovery'], self.bundle['widget'], bundle)

    def test_recovery_preload_preserves_original_bytes(self):
        bundle = copy.deepcopy(self.recovery)
        case = bundle['cases']['observe_a']
        raw = base64.b64decode(case['preload'])
        self.assertIn(b'\r\n',raw)
        case['preload'] = base64.b64encode(raw.replace(b'\r\n',b'\n')).decode()
        with self.assertRaisesRegex(ValueError, 'provenance/control'):
            v.validate_recovery(self.spec['recovery'], self.bundle['widget'], bundle)
