"""Completed policy handoff evidence and its C export stay reproducible."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RepulsionPolicyTests(unittest.TestCase):
    def test_full_live_matrix_preserves_authored_domains_and_runtime_evidence(self):
        data = json.loads((ROOT/'tools/ghidra/fixtures/research/SEP-01.3-expected.json').read_text())
        self.assertEqual(len(data['cases']), 36)
        units = [unit for case in data['cases'] for unit in case['units']]
        self.assertEqual(len(units), 72)
        self.assertEqual({unit['owner'] for unit in units}, {0, 1, 2, 15})
        self.assertEqual({unit['authored'].get('umvt', 'foot') for unit in units},
                         {'foot', 'fly', 'hover', 'amph', 'horse'})
        self.assertTrue(all(case['units'] for case in data['cases']))
        self.assertEqual(data['oracle']['packed_word_cases'], 65536)
        self.assertEqual(len(data['oracle']['inert_pairs']), 24)
        self.assertEqual(len(data['oracle']['inert_tails']), 8)
        self.assertEqual(data['oracle']['harness_selfcheck'], {'pairs': 600, 'tails': 105})

    def test_production_pair_seed_is_aligned_and_geometry_preserves_the_control(self):
        parent = ROOT/'tools/ghidra/fixtures/research/SEP-01.3-expected.json'
        source = json.loads(parent.read_text())['oracle']['inert_pairs']
        fixture = json.loads((ROOT/'tools/ghidra/fixtures/retail-repulsion-inert-producers-1.27.json').read_text())
        self.assertEqual(fixture['source_sha256'], hashlib.sha256(parent.read_bytes()).hexdigest())
        self.assertEqual(len(fixture['pairs']), 24)
        for control, row in zip(source, fixture['pairs']):
            for field in ('source', 'candidate', 'prior', 'vector', 'randomBranch'):
                self.assertEqual(control[field], row[field])
            self.assertEqual(row['ownerBefore'], [4273436052, 209508436])
            for shift in (0, 8, 16, 24):
                self.assertEqual((row['ownerBefore'][1]>>shift)&3, 0)

    def test_c_export_is_exactly_derived_from_frozen_evidence(self):
        subprocess.run([sys.executable, str(ROOT/'tools/ghidra/generate_wc3_repulsion_policy.py'), '--check'], check=True)

    def test_saved_ghidra_names_and_comments_match_mapper(self):
        fixture = json.loads((ROOT/'tools/ghidra/fixtures/retail-repulsion-policy-ghidra-1.27.json').read_text())
        source = (ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        rows = {address:(json.loads('"'+name+'"'), json.loads('"'+comment+'"'))
                for address,name,comment in re.findall(r'\{"(6f[0-9a-f]+)",\s*"((?:\\.|[^"\\])*)",\s*"((?:\\.|[^"\\])*)"\}', source)}
        self.assertFalse(fixture['unsaved_changes'])
        self.assertEqual(len(fixture['functions']), 13)
        for function in fixture['functions']:
            self.assertEqual((function['name'],function['comment']), rows[function['address']])


if __name__ == '__main__':
    unittest.main()
