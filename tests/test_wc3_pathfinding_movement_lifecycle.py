"""Reject lost Stop/turn/occupancy and incomplete original motion contracts."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_movement_lifecycle_trace import render_header,verify_contract

class MovementLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-movement-lifecycle-1.27.json').read_text())
    def test_complete_native_contract_and_literal_engine_inputs(self):
        verify_contract(self.fixture)
        self.assertEqual(len(self.fixture['cases']),2)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_movement_lifecycle.h').read_text(),render_header(self.fixture))
    def test_stop_cannot_retain_translation(self):
        s=copy.deepcopy(self.fixture);next(r for r in s['states']if 'label=after_stop 'in r['marker'])['state'][3]=1
        with self.assertRaises(ValueError):verify_contract(s)
    def test_boundary_position_is_not_a_cell_center(self):
        s=copy.deepcopy(self.fixture);next(r for r in s['states']if 'label=boundary_reset 'in r['marker'])['state'][1]=0x41a40000
        with self.assertRaises(ValueError):verify_contract(s)
    def test_stop_cannot_remove_stationary_occupancy(self):
        s=copy.deepcopy(self.fixture);next(r for r in s['chains']if 'label=boundary_reset 'in r['marker'])['records'][0]['kind']=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_stationary_turn_must_change_facing(self):
        s=copy.deepcopy(self.fixture)
        for r in s['motion']:
            if r[4:6]==[0,0]:r[6]=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_public_zero_request_keeps_authored_minimum(self):
        s=copy.deepcopy(self.fixture);r=[r for r in s['producers']if r['event']=='speed-native'][7];r['output']=0
        with self.assertRaises(ValueError):verify_contract(s)
    def test_complete_motion_markers_chains_and_orders_required(self):
        for key in('motion','markers','chains','destinations'):
            s=copy.deepcopy(self.fixture);s[key].pop()
            with self.assertRaises(ValueError):verify_contract(s)
    def test_changed_position_word_rejected_under_same_extent(self):
        s=copy.deepcopy(self.fixture);s['motion'][400][2]^=1
        with self.assertRaises(ValueError):verify_contract(s)

if __name__=='__main__':unittest.main()
