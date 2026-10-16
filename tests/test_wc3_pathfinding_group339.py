import copy
import gzip
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_group339 as verify


class Group339EvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(verify.SPEC.read_text())
        cls.cases = json.loads(gzip.decompress(verify.BUNDLE.read_bytes()))

    def test_complete_repeats_and_uninstrumented_control(self):
        result = verify.runtime(self.cases, self.spec)
        self.assertEqual(result['native_events'], 1372)
        self.assertEqual(result['public_markers'], 98)

    def test_missing_repeat_rejected(self):
        with self.assertRaises(ValueError):
            verify.runtime(self.cases[:2], self.spec)

    def test_sleep_prevention_change_rejected(self):
        cases = copy.deepcopy(self.cases)
        row = next(r for r in cases[0]['rows'] if r.get('scene') == 12 and
                   r.get('tick') == 568 and r['event'] == 'notify' and r['candidate']['ec'] == 1)
        row['candidate']['ec'] = 0
        with self.assertRaises(ValueError):
            verify.runtime(cases, self.spec)

    def test_binding_priority_change_rejected(self):
        cases = copy.deepcopy(self.cases)
        row = next(r for r in cases[0]['rows'] if r.get('scene') == 1 and r['event'] == 'rank-end')
        row['key'][1] ^= 0x1000
        with self.assertRaises(ValueError):
            verify.runtime(cases, self.spec)

    def test_unfinished_observer_rejected(self):
        cases = copy.deepcopy(self.cases)
        next(r for r in cases[0]['rows'] if r['event'] == 'trace-end')['depth'] = 1
        with self.assertRaises(ValueError):
            verify.runtime(cases, self.spec)

    def test_instrumented_control_rejected(self):
        cases = copy.deepcopy(self.cases)
        cases[-1]['rows'].insert(1, {'event': 'marker', 'seq': 1})
        with self.assertRaises(ValueError):
            verify.runtime(cases, self.spec)

    def test_public_control_change_rejected(self):
        cases = copy.deepcopy(self.cases)
        cases[-1]['preload'] = cases[-1]['preload'].replace('label=setup', 'label=changed', 1)
        with self.assertRaises(ValueError):
            verify.runtime(cases, self.spec)

    def test_capture_provenance_change_rejected(self):
        cases = copy.deepcopy(self.cases)
        cases[0]['rows'][0]['source_sha256']['map'] = '0' * 64
        with self.assertRaises(ValueError):
            verify.runtime(cases, self.spec)


if __name__ == '__main__':
    unittest.main()
