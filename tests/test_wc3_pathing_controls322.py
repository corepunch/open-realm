"""The observer-free closure requires every scene and both complete controls."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from verify_wc3_pathing_controls322 import FIXTURE, validate


class RepeatedControlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(FIXTURE.read_text())
        cls.bundle = json.loads(gzip.decompress(FIXTURE.with_suffix('.json.gz').read_bytes()))

    def test_complete_repeated_controls(self):
        result = validate(self.spec, self.bundle)
        self.assertEqual(result['repeated_scenes'], 4)
        self.assertEqual(result['control_markers'], 3072)

    def test_missing_scene_or_repeat_rejected(self):
        for change in ('scene', 'repeat'):
            bundle = copy.deepcopy(self.bundle)
            if change == 'scene':
                del bundle['blocked_goal']
            else:
                bundle['blocked_goal']['controls'].pop()
            with self.assertRaises(ValueError):
                validate(self.spec, bundle)

    def test_rehashed_corruption_cannot_change_expected_trajectory(self):
        for field, old, new in [('preload', 'label=complete', 'label=incomplete'),
                                ('preload', 'label=sample', 'label=missing')]:
            spec, bundle = copy.deepcopy(self.spec), copy.deepcopy(self.bundle)
            control = bundle['blocked_goal']['controls'][1]
            control[field] = control[field].replace(old, new, 1)
            spec['scenes']['blocked_goal']['controls'][1][field] = hashlib.sha256(control[field].encode()).hexdigest()
            with self.assertRaises(ValueError):
                validate(spec, bundle)

    def test_rehashed_failed_or_attached_control_rejected(self):
        for field, value in [('error', 'missing output'), ('transport', 'attached'), ('display', ':94')]:
            spec, bundle = copy.deepcopy(self.spec), copy.deepcopy(self.bundle)
            control = bundle['blocked_goal']['controls'][1]
            provenance = json.loads(control['provenance'])
            provenance[field] = value
            control['provenance'] = json.dumps(provenance)
            spec['scenes']['blocked_goal']['controls'][1]['provenance'] = hashlib.sha256(control['provenance'].encode()).hexdigest()
            with self.assertRaises(ValueError):
                validate(spec, bundle)


if __name__ == '__main__':
    unittest.main()
