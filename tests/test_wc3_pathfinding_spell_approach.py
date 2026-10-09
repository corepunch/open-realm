import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('spell185', ROOT / 'tools/frida/research/spell185_expected.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
EXPECTED = ROOT / 'tools/ghidra/fixtures/retail-spell-approach185-1.27.json'
HEADER = ROOT / 'games/warcraft-3/game/tests/fixtures/retail_spell_approach185.h'


class SpellApproachEvidence(unittest.TestCase):
    def setUp(self):
        self.expected = json.loads(EXPECTED.read_text())

    def test_header_is_complete_observed_motion(self):
        self.assertEqual(HEADER.read_text(), module.header(self.expected['motion']))
        self.assertEqual(len(self.expected['motion']), 50)
        self.assertEqual(self.expected['motion'][0][0], 1091)
        self.assertEqual(self.expected['motion'][-1][0], 1140)
        self.assertEqual({r[8] for r in self.expected['motion']}, {0x41d7c000})

    def test_changed_motion_detaches_engine_fixture(self):
        for column in range(9):
            changed = copy.deepcopy(self.expected['motion'])
            changed[0][column] ^= 1
            with self.subTest(column=column):
                self.assertNotEqual(module.header(changed), HEADER.read_text())

    def test_missing_motion_is_rejected(self):
        with self.assertRaises(ValueError):
            module.header(self.expected['motion'][:-1])

    def test_identity_normalization_preserves_simulation(self):
        self.assertEqual(module.state({'p': 'address', 'id': [7, 8], 'path': {'cnt': [4, 26], 'range': 0x41d7c000},
                                       'pos': [0x41100000, 0x41100000]}),
                         {'path': {'cnt': [4, 26], 'range': 0x41d7c000}, 'pos': [0x41100000, 0x41100000]})

    def test_three_repeats_and_uninstrumented_control_are_pinned(self):
        pins = self.expected['captures']
        self.assertEqual([p['metadata']['mode'] for p in pins], ['observe', 'observe', 'observe', 'control'])
        # Preload emits its own wall-clock header; public marker strings are
        # compared exactly by the verifier, while whole-file hashes pin each run.
        self.assertTrue(all(len(p['preload_sha256']) == 64 for p in pins))
        for pin in pins:
            self.assertEqual(pin['metadata']['sha256'], module.HASH)
            self.assertEqual(pin['metadata']['source_sha256']['map'], self.expected['map_sha256'])
            self.assertTrue(pin['metadata']['owned'])

    def test_failed_or_unowned_capture_is_rejected(self):
        for row in ({'event': 'trace-failed'}, {'event': 'metadata', 'sha256': module.HASH, 'owned': False}):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'capture.jsonl'
                path.write_text(json.dumps(row) + '\n')
                with self.assertRaises(ValueError):
                    module.extract(path)


if __name__ == '__main__':
    unittest.main()
