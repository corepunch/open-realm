#!/usr/bin/env python3
"""Keep copied base profiles and incomplete witnesses out of target parity."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_mask362 as verifier


class TargetMasks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(verifier.SPEC.read_text())

    def reject(self,change):
        spec=copy.deepcopy(self.spec);change(spec)
        with self.assertRaises(ValueError):verifier.validate(spec)

    def test_original_profile_literal_rows_and_fresh_geometry_are_complete(self):
        self.assertEqual(len(verifier.validate(self.spec)['source_runs'][0]['scenes']['6']['rows']),617)

    def test_copied_footman_radius_is_rejected(self):
        self.reject(lambda s:s['authored_profile'].__setitem__('collision',31))

    def test_copied_footman_turn_is_rejected(self):
        self.reject(lambda s:s['authored_profile'].__setitem__('turnRate',.6))

    def test_incomplete_original_instruction_inventory_is_rejected(self):
        self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))

    def test_geometry_must_belong_to_the_original_and_fresh_captures(self):
        self.reject(lambda s:s['geometry_witnesses'][1].__setitem__('capture_sha256','0'*64))

    def test_persistent_geometry_witness_cannot_be_omitted(self):
        self.reject(lambda s:s['geometry_witnesses'][0]['rows'].pop())

    def test_shortened_production_owner_stream_is_rejected(self):
        source=(ROOT/'games/warcraft-3/game/tests/fixtures/retail_mask362.h').read_text()
        lines=source.splitlines();del lines[2]
        with self.assertRaises(ValueError):verifier.fixture_rows('\n'.join(lines))


if __name__=='__main__':unittest.main()
