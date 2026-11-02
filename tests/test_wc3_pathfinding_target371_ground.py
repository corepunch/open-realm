#!/usr/bin/env python3
"""Reject altered Ground command producers and minimum-range boundary evidence."""
import copy,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_target371_ground as verifier


class GroundCommandEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(verifier.SPEC.read_text());cls.groups=verifier.validate(cls.spec)

    def reject(self,change):
        groups=copy.deepcopy(self.groups);change(groups)
        with self.assertRaises((ValueError,AssertionError,StopIteration,KeyError)):
            verifier.runtime(groups,self.spec)

    def test_complete_independent_repeats_and_controls(self):
        result=verifier.runtime(self.groups,self.spec)
        self.assertEqual((result['observed_captures'],result['controls'],result['public_markers'],result['native_records']),
                         (12,6,5928,5923))

    def test_blocked_human_computer_scope_is_required(self):
        self.reject(lambda groups:groups.pop('ground-ai'))

    def test_human_recovery_must_retain_public_head(self):
        def change(groups):
            row=next(r for r in groups['ground-ai'][0]['rows']if r.get('event')=='ground-recover-leave')
            row['unit']['head'][0]+=1
        self.reject(change)

    def test_computer_transport_null_precedes_guard_replacement(self):
        def change(groups):
            row=next(r for r in groups['ground-ai'][0]['rows']if r.get('event')=='ground-transport-leave')
            row['found']=True
        self.reject(change)

    def test_computer_preserves_actual_guard_point(self):
        def change(groups):
            row=next(r for r in groups['ground-ai'][0]['rows']if r.get('event')=='ground-guard-return'and r['tick']==221)
            row['point'][0]^=1
        self.reject(change)

    def test_rooted_no_request_scope_is_required(self):
        self.reject(lambda groups:groups.pop('ground-rooted'))

    def test_rooted_cannot_gain_a_physical_request(self):
        def change(groups):
            groups['ground-rooted'][0]['rows'].insert(2,dict(event='point-request',scene=0))
        self.reject(change)

    def test_missing_repeat(self):
        self.reject(lambda groups:groups['ground'].pop(1))

    def test_missing_actual_equality_and_adjacent_boundaries(self):
        self.reject(lambda groups:groups.pop('ground-boundary'))

    def test_control_cannot_have_hooks(self):
        self.reject(lambda groups:groups['ground'][2]['rows'].insert(1,dict(event='ground-validation')))

    def test_incomplete_public_capture(self):
        self.reject(lambda groups:groups['ground-boundary'][2]['rows'][-1].__setitem__('complete',False))

    def test_public_stream_must_match_recorded_digest(self):
        self.reject(lambda groups:groups['ground'][2].__setitem__('preload',groups['ground'][2]['preload']+'\n'))

    def test_already_in_range_must_use_actual_flt_max(self):
        def change(groups):
            row=next(r for r in groups['ground'][0]['rows']if r.get('event')=='point-request'and r['scene']==1)
            row['range']=0x42b40000
        self.reject(change)

    def test_minimum_range_equality_cannot_be_admitted(self):
        def change(groups):
            row=next(r for r in groups['ground-boundary'][0]['rows']if r.get('event')=='ground-validation'and r['scene']==3)
            row['result']=0
        self.reject(change)

    def test_true_adjacent_float_cannot_be_substituted(self):
        def change(groups):
            row=next(r for r in groups['ground-boundary'][0]['rows']if r.get('event')=='point-request'and r['scene']==2)
            row['point'][0]-=1
        self.reject(change)

    def test_whole_heading_state_retained(self):
        def change(groups):
            row=next(r for r in groups['ground'][0]['rows']if r.get('event')=='velocity')
            row['after']['pose'][7]^=1
        self.reject(change)

    def test_supplemental_queue_family_required(self):
        self.reject(lambda groups:groups.pop('ground-ai-queue'))

    def test_fully_flushed_birth_control_required(self):
        self.reject(lambda groups:groups.pop('ground-ai-birth'))

    def test_queue_depth_remains_literal(self):
        def change(groups):
            row=next(r for r in groups['ground-ai-queue'][0]['rows']if r.get('event')=='queue-ai-scope-enter')
            row['unit']['depth']+=1
        self.reject(change)

    def test_no_caller_scalar_exception(self):
        def change(groups):
            row=next(r for r in groups['ground-ai-birth'][0]['rows']if r.get('event')=='queue-depth-release-enter')
            row['caller']+=65536
        self.reject(change)

    def test_return_proof_code_and_hex_operands_are_literal(self):
        def change(groups):
            row=next(r for r in groups['ground-ai-birth'][0]['rows']if r.get('returnProof'))
            row['returnProof']['instructions'][1]['opStr']='0xfeed0000'
        self.reject(change)

    def test_nonwhitelisted_hex_pointer_is_literal(self):
        def change(groups):
            row=next(r for r in groups['ground-ai-birth'][0]['rows']if r.get('returnProof'))
            row['returnProof']['pointer']='0xfeed0000'
        self.reject(change)

    def test_source_pointer_requires_published_canonical_identity(self):
        def change(groups):
            row=next(r for r in groups['ground-ai-queue'][0]['rows']if isinstance(r.get('unit'),str))
            row['unit']='0xfeed0000'
        self.reject(change)

    def test_canonical_source_identity_cannot_be_removed(self):
        def change(groups):
            row=next(r for r in groups['ground-ai-birth'][0]['rows']if r.get('event')=='queue-internal-dispatch-enter')
            row['unit'].pop('identity')
        self.reject(change)

    def test_birth_cannot_drop_postcleanup_controlled_marker(self):
        self.reject(lambda groups:groups['ground-ai-birth'][2]['rows'][-1].__setitem__('markers',99))

    def test_complete_original_instruction_inventory_required(self):
        spec=copy.deepcopy(self.spec);spec['instructions'].pop(next(iter(spec['instructions'])))
        with self.assertRaises(ValueError):verifier.validate(spec)


if __name__=='__main__':unittest.main()
