"""Reject premature/global terrain publication and incomplete native movement."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_terrain_cache_trace import render_header,verify_contract

class TerrainPublicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-terrain-cache-1.27.json').read_text())
    def test_complete_native_publication_and_literal_engine_inputs(self):
        verify_contract(self.fixture)
        self.assertEqual(len(self.fixture['cases']),2)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_terrain_cache.h').read_text(),render_header(self.fixture))
    def test_terrain_write_must_not_publish_adaptive_lanes(self):
        s=copy.deepcopy(self.fixture);s['geometry'][1]['hierarchy'][0]['values'][17][0]=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_first_footprint_must_not_publish_remote_patch(self):
        s=copy.deepcopy(self.fixture);s['geometry'][3]['hierarchy'][0]['values'][204][0]=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_second_footprint_must_publish_remote_patch(self):
        s=copy.deepcopy(self.fixture);s['geometry'][5]['hierarchy'][0]['values'][204][0]=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_removal_retains_authored_terrain_edits(self):
        s=copy.deepcopy(self.fixture);s['geometry'][3]['masks'][66]=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_fine_retry_must_retain_adaptive_ownership(self):
        s=copy.deepcopy(self.fixture);r=next(r for r in s['producers']if r['event']=='search'and r['pops']==94);r['kind']='acc'
        with self.assertRaises(ValueError):verify_contract(s)
    def test_all_parent_levels_and_complete_lifetime_are_required(self):
        for change in ('parent','marker','motion'):
            s=copy.deepcopy(self.fixture)
            if change=='parent':s['geometry'][5]['hierarchy'].pop()
            elif change=='marker':s['markers'].pop()
            else:s['motion'].pop()
            with self.assertRaises(ValueError):verify_contract(s)
    def test_motion_words_cannot_change_under_same_extent(self):
        s=copy.deepcopy(self.fixture);s['motion'][20][2]^=1
        with self.assertRaises(ValueError):verify_contract(s)

if __name__=='__main__':unittest.main()
