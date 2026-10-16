"""Reject missing modes and corrupted timing, trajectories or no-hook provenance."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_controls324 as v


class CrowdModeControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(v.SPEC.read_text())
        cls.bundle = json.loads(gzip.decompress(v.SPEC.with_suffix('.json.gz').read_bytes()))

    def test_complete_seven_controls(self):
        result = v.validate(self.spec, self.bundle)
        self.assertEqual(result['repeated_controls'], 7)
        self.assertEqual(result['repeated_markers'], 4416)

    def test_every_crowd_and_mode_required(self):
        for name in v.SCENES:
            with self.subTest(name=name):
                bundle = copy.deepcopy(self.bundle)
                del bundle['controls'][name]
                with self.assertRaisesRegex(ValueError, 'missing scene'):
                    v.validate(self.spec, bundle)

    def test_cannot_drop_original_source_pins(self):
        spec = copy.deepcopy(self.spec)
        spec['source_sha256'] = {}
        with self.assertRaisesRegex(ValueError, 'missing original source pins'):
            v.validate(spec, self.bundle)

    def test_rehashed_trajectory_or_timing_still_rejected(self):
        for name, old, new in [('crowd', 'tick=312', 'tick=313'),
                               ('ground', 'label=complete', 'label=incomplete'),
                               ('fly', 'x=768.0000', 'x=768.0001'),
                               ('float', 'order=851986', 'order=0')]:
            with self.subTest(name=name):
                spec, bundle = copy.deepcopy(self.spec), copy.deepcopy(self.bundle)
                control = bundle['controls'][name]
                self.assertIn(old, control['preload'])
                control['preload'] = control['preload'].replace(old, new, 1)
                spec['scenes'][name]['preload'] = v.sha(control['preload'].encode())
                with self.assertRaisesRegex(ValueError, 'trajectory/timing'):
                    v.validate(spec, bundle)

    def test_rehashed_invalid_provenance_still_rejected(self):
        for field, value in [('error', 'timeout'), ('mode', 'observe'), ('transport', 'attached'),
                             ('env', 'A'), ('map_sha256', 'wrong'), ('already_exited', True),
                             ('loading_input', []), ('complete', False), ('helper_sha256', 'wrong')]:
            with self.subTest(field=field):
                spec, bundle = copy.deepcopy(self.spec), copy.deepcopy(self.bundle)
                control = bundle['controls']['crowd']
                provenance = json.loads(control['provenance'])
                provenance[field] = value
                control['provenance'] = json.dumps(provenance)
                spec['scenes']['crowd']['provenance'] = v.sha(control['provenance'].encode())
                with self.assertRaises(ValueError):
                    v.validate(spec, bundle)

    def test_rehashed_reference_cannot_replace_retail(self):
        spec, bundle = copy.deepcopy(self.spec), copy.deepcopy(self.bundle)
        reference = json.loads(bundle['reference'])
        reference['crowd']['public'].pop()
        bundle['reference'] = json.dumps(reference)
        spec['reference_sha256'] = v.sha(bundle['reference'].encode())
        with self.assertRaisesRegex(ValueError, 'original reference changed'):
            v.validate(spec, bundle)


if __name__ == '__main__':
    unittest.main()
