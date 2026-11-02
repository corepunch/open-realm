"""Frozen original reveal masks retain mutation ordering and read-only provenance."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_target_reveal217 as V

class TargetRevealContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(V.FIXTURE.read_text())
        cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))

    def reject(self,change):
        b=copy.deepcopy(self.bundle);change(b)
        with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)

    def mutate(self,event,field,value):
        self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r['event']==event).__setitem__(field,value))

    def test_original_repeats_and_unhooked_control(self):
        self.assertEqual(V.validate_runtime(self.bundle,V.validate(self.spec)),
            dict(captures=2,controls=1,queries=1468,shares=36,losses=80,public_markers=186))

    def test_every_source_is_pinned(self):
        for path in V.SOURCES:
            spec=copy.deepcopy(self.spec);spec['pins'][path]='0'*64
            with self.assertRaises(ValueError):V.validate(spec)

    def test_query_flags_modes_and_masks_are_required(self):
        for field,value in [('flags',1),('mode',0),('masks',[8,15]),('targetFlags',0)]:
            self.mutate('query',field,value)

    def test_reveal_mask_capture_is_not_live_alliance_recalculation(self):
        self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r['event']=='reveal-state'and r['masks']==[2,15]).__setitem__('masks',[2,7]))

    def test_same_value_share_calls_cannot_be_folded(self):
        self.reject(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows']if r['event']=='share'and r['enabled']==1 and r['before']==[2,15])))

    def test_share_call_recipient_and_before_state_are_exact(self):
        for field,value in [('caller','wrong'),('player',0),('enabled',0),('before',[8,15])]:
            self.mutate('share',field,value)

    def test_no_synchronous_target_loss_is_invented_on_unshare(self):
        self.reject(lambda b:b['captures'][0]['rows'].insert(2,dict(event='lost',seq=90000,caller='699540',masks=[0,0])))

    def test_only_movement_fallback_is_sequence_compared(self):
        b=copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows']if r['event']=='fallback'and r['caller']!='66fe40')['masks']=[123,456]
        self.assertEqual(V.validate_runtime(b,self.spec)['queries'],1468)
        self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r['event']=='fallback'and r['caller']=='66fe40').__setitem__('masks',[123,456]))

    def test_each_repeat_and_control_is_required(self):
        for i in range(3):self.reject(lambda b:b['captures'].pop(i))
        self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))

    def test_read_only_completion_and_provenance_are_required(self):
        self.mutate('trace-end','readOnly',False)
        self.reject(lambda b:b['captures'][0]['rows'].insert(1,dict(event='trace-failed')))
        self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
        self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
        self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].update(map='0'*64))
        self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].__setitem__('target217_observer.js','0'*64))

    def test_public_positions_and_control_markers_are_required(self):
        for i in range(3):
            self.reject(lambda b:b['captures'][i].__setitem__('preload',b['captures'][i]['preload'].replace('label=shared','label=wrong')))

if __name__=='__main__':unittest.main()
