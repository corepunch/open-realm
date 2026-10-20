#!/usr/bin/env python3
"""Reject incomplete per-owner inverses inside the controlled removal304 family."""
import copy,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_pause379 as v
class PauseOwnerRelease(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.SPEC.read_text());cls.bundle,cls.prior=v.validate(cls.spec)
    def reject(self,change):
        bundle=copy.deepcopy(self.bundle);change(bundle)
        with self.assertRaises((ValueError,AssertionError,KeyError)):
            v.removal.validate_runtime(bundle,self.prior);v.native(bundle)
    def test_complete_controlled_family_and_all_owner_snapshots(self):
        self.assertEqual(len(v.native(self.bundle)),12)
        self.assertEqual(sum(len(r['owners'])for r in v.native(self.bundle)),48)
    def test_pending_removal_must_release_later_move_owner(self):
        def change(b):
            row=next(r for r in b['captures'][0]['rows']if r.get('scene')==2 and r.get('event')=='remove-exit')
            row['state']['abilities'][-1]['disabled']=[1,1,0]
        self.reject(change)
    def test_order_counter_inverse_is_independent(self):
        def change(b):
            row=next(r for r in b['captures'][1]['rows']if r.get('scene')==3 and r.get('event')=='inactivity-enter')
            row['state']['abilities'][1]['disabled']=[0,1,0]
        self.reject(change)
    def test_native_inventory_is_not_reduced_to_engine_fixture(self):
        def change(b):
            row=next(r for r in b['captures'][0]['rows']if r.get('scene')==2 and r.get('event')=='remove-enter')
            row['state']['abilities'].pop(2)
        self.reject(change)
    def test_control_is_required(self):self.reject(lambda b:b['captures'].pop())
    def test_complete_original_inventory(self):
        spec=copy.deepcopy(self.spec);spec['instructions'].pop(next(iter(spec['instructions'])))
        with self.assertRaises(ValueError):v.validate(spec)
if __name__=='__main__':unittest.main()
