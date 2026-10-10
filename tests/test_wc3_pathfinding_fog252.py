"""Reject weakened fog cadence, changed retail trajectories and incomplete controls."""
import copy
import gzip
import json
import sys
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_fog252 as v

class Fog252Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(v.FIXTURE.read_text())
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def test_complete_original_repeats_and_unhooked_control(self):
        r = v.verify_runtime(self.bundle, self.spec)
        self.assertEqual((r['fog_compositions'], r['public_markers'], r['hidden_visits']), (490, 3201, 214))

    def test_rejects_changed_period(self):
        b = copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows'] if r['event'] == 'fog-compose')['period'] = 0x3ecccccd
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_earlier_publication(self):
        b = copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows'] if r['event'] == 'fog-compose' and r['c'] == 1304)['c'] = 1300
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_changed_original_destination(self):
        b = copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows'] if r['event'] == 'gtick' and r['target'][0] != 0xffffffff)['path']['dest'][0] ^= 1
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_partial_capture(self):
        b = copy.deepcopy(self.bundle)
        b['captures'][0]['rows'][-1]['complete'] = False
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_instrumented_control(self):
        b = copy.deepcopy(self.bundle)
        b['captures'][2]['rows'].insert(1, dict(event='fog-compose'))
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_preload_newline_normalization(self):
        b = copy.deepcopy(self.bundle)
        b['captures'][0]['preload'] = b['captures'][0]['preload'].replace('\r\n', '\n')
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_changed_source(self):
        s = copy.deepcopy(self.spec)
        s['pins'][v.HEADER] = '0' * 64
        with self.assertRaises(ValueError):
            v.validate(s)

if __name__ == '__main__':
    unittest.main()
