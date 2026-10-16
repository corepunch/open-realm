"""Retail save/load captures must keep their order and exact continuation proof."""
import copy
import gzip
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('spatial_save', ROOT/'tools/ghidra/verify_wc3_pathing_spatial_save.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SpatialSaveTests(unittest.TestCase):
    def test_full_capture_reconstruction_keeps_four_exact_resumed_movers(self):
        report = MODULE.captures()
        self.assertEqual(report['matched_movers'], 4)
        self.assertEqual(report['resumed_commits'], 308)
        self.assertEqual(report['captured_files'], 16)
        self.assertEqual(report['frozen_sha256'],
                         '0856f6f36fecb20f81f0b9b17b4e091c926d325632055de94ac952b8937d8249')

    def test_original_insertion_fixture_covers_all_load_orders_and_clipping(self):
        data = json.loads((MODULE.FIXTURES/'retail-spatial-load-insertion-1.27.json').read_text())
        self.assertEqual(len(data['cases']), 24)
        rectangles = {tuple(row['rectangle']) for row in data['cases']}
        self.assertEqual(len(rectangles), 4)
        for rectangle in rectangles:
            rows = [r for r in data['cases'] if tuple(r['rectangle']) == rectangle]
            self.assertEqual({tuple(r['save_order']) for r in rows}, set(itertools.permutations(range(3))))
        for row in data['cases']:
            self.assertEqual(set(row['before']), set(row['after']))
            self.assertTrue(all(v == row['candidates'] for v in row['after'].values()))
            self.assertEqual(row['records'], len(row['after'])*3)
        self.assertTrue(any(row['before'] != row['after'] for row in data['cases']))

    def test_damaged_capture_cannot_certify_the_control_suffix(self):
        bundle = json.loads(gzip.decompress((MODULE.FIXTURES/'retail-spatial-save-inputs-1.27.json.gz').read_bytes()))
        name = 'ui_route-saveload-observe-2.jsonl'
        with tempfile.TemporaryDirectory() as directory:
            fixtures = Path(directory)
            (fixtures/'research').mkdir()
            (fixtures/'research/MAP-06.2-expected.json').write_bytes(
                (MODULE.FIXTURES/'research/MAP-06.2-expected.json').read_bytes())
            for update_hash in (False, True):
                changed = copy.deepcopy(bundle)
                changed['files'][name] += '{}\n'
                if update_hash:
                    changed['sha256'][name] = hashlib.sha256(changed['files'][name].encode()).hexdigest()
                (fixtures/'retail-spatial-save-inputs-1.27.json.gz').write_bytes(gzip.compress(json.dumps(changed).encode()))
                with patch.object(MODULE, 'FIXTURES', fixtures), self.assertRaises(ValueError):
                    MODULE.captures()

    def test_failed_load_attempts_remain_excluded(self):
        bundle = json.loads(gzip.decompress((MODULE.FIXTURES/'retail-spatial-save-inputs-1.27.json.gz').read_bytes()))
        self.assertEqual(len(bundle['failed_loads']), 3)
        for name in bundle['failed_loads']:
            rows = [json.loads(line) for line in bundle['files'][name].splitlines() if line.strip()]
            self.assertFalse(any(r.get('event') == 'owner-load-end' for r in rows))

    def test_ghidra_readback_matches_persisted_mapper_rows(self):
        fixture = json.loads((MODULE.FIXTURES/'retail-spatial-save-ghidra-1.27.json').read_text())
        source = (ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        rows = {address:(json.loads('"'+name+'"'), json.loads('"'+comment+'"'))
                for address,name,comment in re.findall(r'\{"(6f[0-9a-f]+)",\s*"((?:\\.|[^"\\])*)",\s*"((?:\\.|[^"\\])*)"\}', source)}
        self.assertFalse(fixture['unsaved'])
        self.assertEqual(len(fixture['rows']), 14)
        self.assertEqual(fixture['binary_sha256'], MODULE.BINARY_SHA)
        for row in fixture['rows']:
            self.assertEqual((row['name'],row['comment']),rows[row['address']])


if __name__ == '__main__':
    unittest.main()
