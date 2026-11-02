"""Shared captain scope and owner state stay distinct from full reentry parity."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_shared_trace import render_header,verify_contract


class CaptainSharedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-shared-1.27.json').read_text())

    def test_native_owners_and_literal_engine_footprints(self):
        verify_contract(self.fixture)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_captain_shared.h').read_text(),render_header(self.fixture))

    def test_recycled_slot_cannot_alias_owner_generation(self):
        f=copy.deepcopy(self.fixture);f['shared_state']['identities'][1]=f['shared_state']['identities'][0]
        with self.assertRaises(ValueError):verify_contract(f)

    def test_cached_footprint_survives_live_radius_shrink(self):
        f=copy.deepcopy(self.fixture)
        next(r for r in f['shared_state']['footprints'] if r[0]==1352)[4]=0x3f780000
        with self.assertRaises(ValueError):verify_contract(f)

    def test_largest_mover_in_final_batch_sets_both_paths(self):
        f=copy.deepcopy(self.fixture);f['shared_state']['footprints'][1][3]=0x3f780000
        with self.assertRaises(ValueError):verify_contract(f)

    def test_speed_uses_previous_accumulator(self):
        f=copy.deepcopy(self.fixture);f['shared_state']['publication'][1][2][1]=0x7f7fffff
        with self.assertRaises(ValueError):verify_contract(f)

    def test_two_physical_batches_hold_two_references(self):
        f=copy.deepcopy(self.fixture);f['shared_state']['publication'][0][1][0]=1
        with self.assertRaises(ValueError):verify_contract(f)

    def test_recovery_reentry_gap_cannot_be_hidden(self):
        for key,value in [('whole_engine_parity',True),('recovery_reentry_remains_open',False),('engine_end_msec',13000)]:
            f=copy.deepcopy(self.fixture);f[key]=value
            with self.assertRaises(ValueError):verify_contract(f)

    def test_shared_layout_preserves_unassigned_link_bytes(self):
        f=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-types-1.27.json').read_text())
        shared=next(t for t in f['layouts'] if t['name']=='WC3PathSharedPrefix')
        self.assertEqual(shared['length'],0x2c)
        self.assertEqual([r['offset'] for r in shared['fields']],[0,0x14,0x1c,0x20,0x24,0x28])


if __name__=='__main__':unittest.main()
