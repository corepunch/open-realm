"""Reject incomplete or changed public retail target-policy evidence."""
import base64
import gzip
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('target_warp', ROOT / 'tools/ghidra/research/verify_form013_warp_live.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class TargetWarpTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        data = json.loads(gzip.decompress((ROOT / 'tools/ghidra/fixtures/retail-target-warp-captures-1.27.json.gz').read_bytes()))
        self.paths = []
        for name, text in data.items():
            path = Path(self.temp.name) / name
            path.write_bytes(base64.b64decode(text))
            self.paths.append(path)

    def verify(self):
        return MODULE.verify(*self.paths)

    def mutate(self, index, event, change):
        path = self.paths[index]
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        row = next(r for r in rows if r['event'] == event)
        change(row)
        path.write_text(''.join(json.dumps(r) + '\n' for r in rows))

    def test_complete_repeats_and_observer_free_timeline(self):
        expected = json.loads(gzip.decompress((ROOT / 'tools/ghidra/fixtures/retail-target-warp-live-1.27.json.gz').read_bytes()))
        self.assertEqual(self.verify(), expected)

    def test_changed_warp_argument_is_rejected(self):
        self.mutate(0, 'coarse-request', lambda r: r.update(warp=0))
        with self.assertRaises(ValueError):
            self.verify()

    def test_changed_group_policy_is_rejected(self):
        self.mutate(1, 'group-route', lambda r: r.update(flags=r['flags'] ^ 0x10))
        with self.assertRaises(ValueError):
            self.verify()

    def test_incomplete_observer_is_rejected(self):
        self.mutate(1, 'trace-end', lambda r: r.update(tick=400))
        with self.assertRaises(ValueError):
            self.verify()

    def test_control_input_drift_is_rejected(self):
        self.mutate(2, 'metadata', lambda r: r.update(map='Maps\\other.w3m'))
        with self.assertRaises(ValueError):
            self.verify()

    def test_changed_control_timeline_is_rejected(self):
        path = self.paths[3]
        path.write_text(path.read_text().replace('label=sample', 'label=changed', 1))
        with self.assertRaises(ValueError):
            self.verify()


if __name__ == '__main__':
    unittest.main()
