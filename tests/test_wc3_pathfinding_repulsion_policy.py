"""Completed policy handoff evidence and its C export stay reproducible."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import unittest
import importlib.util

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
            name, comment = rows[function['address']]
            self.assertEqual(function['name'], name)
            self.assertTrue(comment.startswith(function['comment']))

    def test_complete_original_eligibility_retains_signed_counters_and_boolean_return(self):
        fixture = ROOT/'tools/ghidra/fixtures/retail-separation-eligibility-1.27.json'
        self.assertEqual(hashlib.sha256(fixture.read_bytes()).hexdigest(),
                         '6a05aeced48ca5b16df1e889216153e7d3bf3d30aae65ef3d2546c1ef85c4f7c')
        cases = json.loads(fixture.read_text())['cases']
        self.assertEqual(len(cases), 432)
        self.assertEqual({row[-1] for row in cases}, {0, 1})
        for authored in (1, 2, -1, -2147483648, 2147483647):
            self.assertIn([authored, 0, 0, 0, 0, 0, 1], cases)
            self.assertIn([authored, 0, -1, 0, -1, 0, 1], cases)

    def test_new_eligibility_notes_are_saved_and_mapped(self):
        fixture = json.loads((ROOT/'tools/ghidra/fixtures/retail-repulsion-eligibility-ghidra-1.27.json').read_text())
        source = (ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        self.assertFalse(fixture['unsaved_changes'])
        for row in fixture['functions']:
            self.assertIn(row['address'], source)
            self.assertIn(row['note'], source)

    def test_immobile_frida_repeats_and_observer_free_markers_are_complete(self):
        spec = importlib.util.spec_from_file_location('immobile', ROOT/'tools/ghidra/research/verify_immobile_separation.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        result = module.verify(ROOT/'tools/ghidra/fixtures/retail-repulsion-immobile-1.27.json.gz')
        self.assertEqual(result['immobile_markers'], 859)
        self.assertGreater(result['immobile_visits'], 0)
        self.assertGreater(result['immobile_pairs'], 0)


if __name__ == '__main__':
    unittest.main()
