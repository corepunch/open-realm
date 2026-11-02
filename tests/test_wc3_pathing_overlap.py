import copy
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'));sys.path.insert(0,str(ROOT/'tools/frida/research'))
import verify_wc3_pathing_overlap as overlap
import verify_wc3_pathing_proximity as proximity


class OverlapEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.expected=json.loads(gzip.decompress((proximity.FIXTURES/'research/SEP-02.3-expected.json.gz').read_bytes()))
        bundle=json.loads(gzip.decompress((proximity.FIXTURES/'retail-proximity-inputs-1.27.json.gz').read_bytes()))
        cls.rows=[r for r in json.loads(bundle['files']['replay-first.json'])['rows'] if r['i']>=27]

    def check_fixture(self,rows=None):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);proximity.restore_inputs(root)
            return overlap.check_fixture(root,self.expected,self.rows if rows is None else rows)

    def test_complete_c_fixture_covers_every_visit_and_pair(self):
        self.assertEqual(self.check_fixture(),(2546,1882))

    def test_missing_final_visits_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'visit fixture'):self.check_fixture(self.rows[:-1])

    def test_discontinuous_owner_stream_is_rejected(self):
        rows=copy.deepcopy(self.rows);rows[0]['steps'][0]['ownerBefore'][0]^=1
        with self.assertRaisesRegex(ValueError,'discontinuous'):self.check_fixture(rows)

    def test_reordered_engine_neighbors_are_rejected(self):
        parse=overlap.c_array
        def reordered(source,name):
            rows=parse(source,name)
            if name=='overlap_pairs':rows[0],rows[1]=rows[1],rows[0]
            return rows
        with patch.object(overlap,'c_array',reordered),self.assertRaisesRegex(ValueError,'contribution fixture'):
            self.check_fixture()

    def test_endpoint_outcome_change_is_rejected(self):
        rows=copy.deepcopy(self.rows)
        row=next(r for r in rows if r.get('apply',{}).get('valid')==0);row['apply']['valid']=1
        with self.assertRaisesRegex(ValueError,'visit fixture'):self.check_fixture(rows)

    def test_saved_ghidra_owner_random_and_endpoint_mappings(self):
        saved=json.loads((proximity.FIXTURES/'retail-overlap-ghidra-1.27.json').read_text())
        self.assertFalse(saved['unsaved']);self.assertEqual(len(saved['rows']),5)
        mapping=(ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        for row in saved['rows']:
            self.assertIn(row['address'],mapping);self.assertIn('Payoff133',row['comment'])


if __name__=='__main__':unittest.main()
