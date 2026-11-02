"""Exceptional inputs and retail fault boundaries must survive evidence validation."""
import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_nonfinite313 as v

class Nonfinite313Evidence(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads(v.SPEC.read_text())
        self.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def reject(self, edit):
        edit()
        with self.assertRaises(ValueError):
            v.validate(self.spec, self.bundle)

    def event(self, event, case='all-8'):
        return next(r for r in self.bundle[case][0]['rows'] if r['event'] == event)

    def test_complete_contract(self):
        self.assertEqual(v.validate(self.spec, self.bundle), dict(captures=68, native_events=4872, public_markers=812, states=126, fault_captures=8))

    def test_missing_case(self):
        self.reject(lambda: self.bundle.pop('create-9'))

    def test_missing_control_repeat(self):
        self.reject(lambda: self.bundle['radius'].pop())

    def test_authored_nan_cannot_be_sanitized(self):
        self.reject(lambda: self.event('authored-radius', 'radius').update(radius=0x3f800000))

    def test_changed_physical_radius(self):
        self.reject(lambda: self.event('delta-leave', 'radius')['m'].update(radius=0))

    def test_coordinate_producer_is_required(self):
        self.reject(lambda: self.event('public-default-height').update(value=0))

    def test_missing_native_event(self):
        self.reject(lambda: self.bundle['all-8'][0]['rows'].remove(self.event('delta-leave')))

    def test_wrong_fault_site(self):
        self.reject(lambda: self.event('native-exception', 'create-9').update(rva='0x1ddac0'))

    def test_fault_cannot_be_claimed_as_return(self):
        self.reject(lambda: self.spec['cases']['create-9']['states'][-1].update(returned=True))

    def test_fault_requires_terminal_process(self):
        self.reject(lambda: self.event('owned-detached', 'create-9').update(reason='application-requested'))

    def test_old_engine_words_cannot_replace_retail(self):
        self.reject(lambda: self.spec['cases']['all-8']['states'][1]['fine'].__setitem__(0, 0))

    def test_missing_instruction(self):
        self.reject(lambda: self.spec['instructions'].pop(next(iter(self.spec['instructions']))))

    def test_hooked_control(self):
        self.reject(lambda: self.bundle['all-8'][2]['rows'].insert(1, dict(event='trace-end')))

if __name__ == '__main__':
    unittest.main()
