import gzip
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tools/ghidra/fixtures'
spec = importlib.util.spec_from_file_location('route012_summary', ROOT / 'tools/frida/research/route012_summarize.py')
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


class RouteBufferEvidenceTests(unittest.TestCase):
    def test_public_axis_repeats_and_observer_free_control(self):
        paths = [FIXTURES / ('retail-outside-axis-' + variant + '-1.27.jsonl.gz')
                 for variant in ('first', 'repeat', 'control')]
        actual = summary.summarize(*paths)
        self.assertEqual(actual, json.loads((FIXTURES / 'retail-outside-axis-1.27.json').read_text()))
        self.assertEqual([actual['runs'][0]['endpoints'][i]['fine_words'] for i in (0, 2, 4)],
                         [[0xc0600000, 0x41240000], [0x428c8000, 0x41240000], [0x41240000, 0xbf000000]])

    def test_incomplete_capture_is_rejected(self):
        source = FIXTURES / 'retail-outside-axis-first-1.27.jsonl.gz'
        rows = [json.loads(line) for line in summary.read(source).splitlines()]
        for row in rows:
            if row['event'] == 'preload-file':
                row['complete'] = False
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'first.jsonl'
            path.write_text(''.join(json.dumps(row) + '\n' for row in rows))
            (path.parent / 'first-preload.txt').write_bytes(summary.read(FIXTURES / 'retail-outside-axis-first-1.27-preload.txt.gz'))
            with self.assertRaises(AssertionError):
                summary.validate_run(path, 'observe')
        with self.assertRaises(AssertionError):
            summary.validate_run(source, 'control')

    def test_engine_stale_chain_literals_equal_frozen_original(self):
        expected = json.loads((FIXTURES / 'research/ROUTE-01.2-expected.json').read_text())['stale_outside']
        header = (ROOT / 'games/warcraft-3/game/tests/retail_stale_route.h').read_text()
        arrays = re.findall(r'retail_stale_route_\d+\[\]\[2\]=\{(.*?)\n\};', header, re.S)
        self.assertEqual(len(arrays), 2)
        for array, row in zip(arrays, expected):
            self.assertEqual([int(word, 16) for word in re.findall(r'0x([0-9a-f]+)u', array)], row['words'])


if __name__ == '__main__':
    unittest.main()
