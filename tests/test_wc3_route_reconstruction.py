"""Frozen producer words and saved annotations must authenticate adapter tests."""
import copy
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra/research'))
from verify_route01_1_reconstruction import engine_fixture, check_saved_evidence


class ReconstructionEvidenceTests(unittest.TestCase):
    def test_literal_adapter_fixture_matches_original_producer_words(self):
        with gzip.open(ROOT / 'tools/ghidra/fixtures/research/ROUTE-01.1-expected.json.gz', 'rt') as stream:
            frozen = json.load(stream)
        actual, rows = engine_fixture(frozen)
        self.assertEqual(rows, 200)
        self.assertEqual(actual, (ROOT / 'games/warcraft-3/game/tests/retail_reconstruction.h').read_text())
        changed = copy.deepcopy(frozen)
        changed['fine'][0]['words'][0] ^= 1
        self.assertNotEqual(engine_fixture(changed)[0], actual)

    def test_saved_evidence_rejects_unsaved_missing_duplicate_and_unmapped_functions(self):
        path = ROOT / 'tools/ghidra/fixtures/retail-route-reconstruction-ghidra-1.27.json'
        saved = json.loads(path.read_text())
        self.assertEqual(check_saved_evidence(path, saved['binary_sha256']), 7)
        with tempfile.TemporaryDirectory() as directory:
            altered = Path(directory) / 'saved.json'
            for mutation in range(5):
                changed = copy.deepcopy(saved)
                if mutation == 0:
                    changed['unsaved'] = True
                elif mutation == 1:
                    changed['rows'].pop()
                elif mutation == 2:
                    changed['rows'].append(changed['rows'][0])
                elif mutation == 3:
                    changed['rows'][0]['comment'] = 'unverified'
                else:
                    changed['binary_sha256'] = 'wrong binary'
                altered.write_text(json.dumps(changed))
                with self.assertRaises(AssertionError):
                    check_saved_evidence(altered, saved['binary_sha256'])
