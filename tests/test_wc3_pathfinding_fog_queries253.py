"""Keep the retail fog-query contract stronger than the new engine implementation."""
import copy
import gzip
import json
import sys
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_fog_queries253 as v

class FogQueries253Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(v.FIXTURE.read_text())
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def test_original_repeats_control_and_location_parity(self):
        self.assertEqual(v.verify_runtime(self.bundle, self.spec), dict(captures=2, controls=1, public_markers=96))

    def test_rejects_changed_owner_counter(self):
        b = copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows'] if r['event'] == 'marker')['c'] += 1
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_changed_publication_deadline(self):
        b = copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows'] if r['event'] == 'fog-compose' and r['c'] == 1104)['c'] -= 1
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_noop_radius(self):
        b = copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows'] if r['event'] == 'fog-create')['radius'] = 0x42c00000
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_missing_completion(self):
        b = copy.deepcopy(self.bundle)
        b['captures'][0]['rows'][-1]['complete'] = False
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_instrumented_control(self):
        b = copy.deepcopy(self.bundle)
        b['captures'][2]['rows'].insert(1, dict(event='fog-compose'))
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_newline_rewriting(self):
        b = copy.deepcopy(self.bundle)
        b['captures'][0]['preload'] = b['captures'][0]['preload'].replace('\r\n', '\n')
        with self.assertRaises(ValueError):
            v.verify_runtime(b, self.spec)

    def test_rejects_changed_native_classification(self):
        s = copy.deepcopy(self.spec)
        s['classification'][0] = 1
        with self.assertRaises(ValueError):
            v.verify_runtime(self.bundle, s)

    def test_rejects_changed_probe(self):
        s = copy.deepcopy(self.spec)
        s['pins']['tools/frida/research/target253_probe.j'] = '0'*64
        with self.assertRaises(ValueError):
            v.validate(s)

if __name__ == '__main__':
    unittest.main()
