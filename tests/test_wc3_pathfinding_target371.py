#!/usr/bin/env python3
"""Reject incomplete or weakened actual point-command evidence."""
import copy,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_target371 as verifier


class PointCommandEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(verifier.SPEC.read_text());cls.groups=verifier.validate(cls.spec)

    def reject(self,change):
        groups=copy.deepcopy(self.groups);change(groups)
        with self.assertRaises((ValueError,AssertionError,StopIteration,KeyError)):
            verifier.runtime(groups,self.spec)

    def test_complete_repeats_and_unhooked_controls(self):
        result=verifier.runtime(self.groups,self.spec)
        self.assertEqual((result['observed_captures'],result['controls'],result['public_markers'],result['native_records']),
                         (4,2,3396,3422))

    def test_missing_repeat(self):
        self.reject(lambda groups:groups['exact'].pop(1))

    def test_missing_static_and_adjacent_boundary_variant(self):
        self.reject(lambda groups:groups.pop('terrain'))

    def test_control_cannot_contain_instrumentation(self):
        self.reject(lambda groups:groups['terrain'][2]['rows'].insert(1,dict(event='module')))

    def test_timed_out_control_is_rejected(self):
        self.reject(lambda groups:groups['exact'][2]['rows'][-1].__setitem__('complete',False))

    def test_public_preload_bound_to_capture(self):
        self.reject(lambda groups:groups['terrain'][2].__setitem__('preload',groups['terrain'][2]['preload']+'\n'))

    def test_raw_static_click_cannot_be_replaced_with_nearby_goal(self):
        def change(groups):
            row=next(r for r in groups['terrain'][0]['rows']if r.get('event')=='point-task')
            row['point'][0]-=1
        self.reject(change)

    def test_late_range_admission_is_rejected(self):
        def change(groups):
            rows=groups['exact'][0]['rows'];row=next(r for r in rows if r.get('event')=='range-publication')
            rows.remove(row);rows.insert(next(i for i,r in enumerate(rows)if r.get('event')=='marker'and'label=issue-after'in r['value'])+1,row)
        self.reject(change)

    def test_whole_heading_state_cannot_be_discarded(self):
        def change(groups):
            row=next(r for r in groups['terrain'][0]['rows']if r.get('event')=='velocity')
            row['after']['pose'][7]^=1
        self.reject(change)

    def test_missing_velocity_call_is_rejected(self):
        def change(groups):
            rows=groups['exact'][0]['rows'];rows.remove(next(r for r in rows if r.get('event')=='velocity'))
        self.reject(change)

    def test_original_inventory_cannot_be_shortened(self):
        spec=copy.deepcopy(self.spec);spec['instructions'].pop(next(iter(spec['instructions'])))
        with self.assertRaises(ValueError):verifier.validate(spec)


if __name__=='__main__':unittest.main()
