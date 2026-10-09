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
    def test_load_captures_require_completed_reset_and_control(self):
        source = ROOT / 'tools/frida/research/route012_search_load_summarize.py'
        spec = importlib.util.spec_from_file_location('route012_load', source)
        load = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(load)
        captures = [FIXTURES / ('retail-route-search-load-' + variant + '-1.27.jsonl.gz')
                    for variant in ('first', 'repeat', 'control')]
        preloads = [FIXTURES / ('retail-route-search-load-' + variant + '-1.27-preload.txt.gz')
                    for variant in ('first', 'repeat', 'control')]
        result = load.summarize(captures, preloads)
        self.assertEqual(result, json.loads((FIXTURES / 'retail-route-search-load-1.27.json').read_text()))
        self.assertEqual(result['public_markers'], 597)
        with tempfile.TemporaryDirectory() as directory:
            damaged = Path(directory) / 'first.jsonl'
            rows = [json.loads(line) for line in load.read(captures[0]).splitlines()]
            for row in rows:
                if row.get('event') == 'search-owners-after-load':
                    row['fine']['source'] = 0
            damaged.write_text(''.join(json.dumps(row) + '\n' for row in rows))
            with self.assertRaises(AssertionError):
                load.summarize([damaged, *captures[1:]], preloads)
            rows = [row for row in rows if row.get('event') != 'search-owners-after-load']
            damaged.write_text(''.join(json.dumps(row) + '\n' for row in rows))
            with self.assertRaises(AssertionError):
                load.summarize([damaged, *captures[1:]], preloads)

    def test_invalid_consumers_include_full_retry_owner_state(self):
        source = ROOT / 'tools/ghidra/research/export_route012_invalid_consumers.py'
        spec = importlib.util.spec_from_file_location('route012_invalid', source)
        exporter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(exporter)
        self.assertEqual(exporter.render(), exporter.OUTPUT.read_text())
        fixture = json.loads(exporter.SOURCE.read_text())
        self.assertEqual(len(fixture['rows']), 56)
        self.assertEqual(fixture['constructors'], {
            kind: dict(source=0xffffffff, nearest=0xffffffff, count=0, budget=0)
            for kind in ('fine', 'coarse')})
        for row in fixture['rows']:
            self.assertEqual(row['rng'], [0x12345678, 0])
            if row['label'] in ('outside_negative_x', 'outside_beyond_x'):
                for visit in ('first', 'second'):
                    self.assertEqual(row[visit]['result'], 1)
                    self.assertEqual(row[visit]['state']['fine_count'], 0)
                    self.assertEqual(row[visit]['state']['fine_index'], 0xffffffff)
                    self.assertEqual(row[visit]['state']['retry'], 7)
                self.assertNotEqual(row['first']['rng'], row['rng'])
                self.assertNotEqual(row['second']['rng'], row['first']['rng'])
            else:
                for visit in ('first', 'second'):
                    self.assertEqual(row[visit]['result'], 0)
                    self.assertEqual(row[visit]['state']['retry'], 0)
                    self.assertEqual(row[visit]['rng'], row['rng'])

    def test_consumer_literals_require_original_distance_initialization(self):
        source = ROOT / 'tools/ghidra/research/export_route012_consumers.py'
        spec = importlib.util.spec_from_file_location('route012_consumers', source)
        exporter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(exporter)
        self.assertEqual(exporter.render(), exporter.OUTPUT.read_text())
        corrected = json.loads(exporter.SOURCE.read_text())
        original = json.loads((FIXTURES / 'research/ROUTE-01.2-expected.json').read_text())
        self.assertEqual(corrected['consumer_initialization']['word'], 0x41200000)
        self.assertEqual(len(corrected['rows']), 264)
        # Zero-BSS captures are controlled inputs, not gameplay expectations.
        old = next(r for r in original['rows'] if r['scenario'] == 'empty_admitted' and r['adaptive_enabled'])
        new = next(r for r in corrected['rows'] if r['scenario'] == 'empty_admitted' and r['adaptive_enabled'])
        self.assertEqual(old['first']['state']['coarse_index'], 1)
        self.assertEqual(new['first']['state']['coarse_index'], 0)
        self.assertNotEqual(old['first']['state']['fine_words'], new['first']['state']['fine_words'])

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
