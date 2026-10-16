"""Frozen original reveal masks retain mutation ordering and read-only provenance."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_target_normalize218 as V

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
            dict(captures=2,controls=1,queries=996,admissions=32,normalized=26,public_markers=174))

    def test_every_source_is_pinned(self):
        for path in V.SOURCES:
            spec=copy.deepcopy(self.spec);spec['pins'][path]='0'*64
            with self.assertRaises(ValueError):V.validate(spec)

    def test_native_validation_error_does_not_imply_refusal(self):
        for field,value in [('result',0),('flags',0),('order',851971)]:
            self.mutate('admission-result',field,value)

    def test_unseen_move_must_discard_target_identity_and_capture_point(self):
        for field,value in [('target',[123,456]),('point',[0,0]),('order',851971),('player',0)]:
            self.mutate('target-order',field,value)

    def test_revealed_hostile_move_must_retain_identity(self):
        self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r['event']=='target-order'and r['target']is not None).__setitem__('target',None))

    def test_task_and_user_head_must_agree(self):
        for field,value in [('head',None),('task',{'code':852339})]:
            self.mutate('order-state',field,value)

    def test_visibility_flags_and_source_masks_are_exact(self):
        for field,value in [('flags',2),('mode',7),('reveal',[8,15]),('masks',[8,8])]:
            self.mutate('query',field,value)

    def test_each_repeat_and_unhooked_control_is_required(self):
        for i in range(3):self.reject(lambda b:b['captures'].pop(i))
        self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))

    def test_read_only_completion_and_provenance_are_required(self):
        self.mutate('trace-end','readOnly',False)
        self.reject(lambda b:b['captures'][0]['rows'].insert(1,dict(event='trace-failed')))
        self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
        self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
        self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].update(map='0'*64))
        self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].__setitem__('target218_observer.js','0'*64))

    def test_public_positions_and_control_markers_are_required(self):
        for i in range(3):
            self.reject(lambda b:b['captures'][i].__setitem__('preload',b['captures'][i]['preload'].replace('label=before_accepted','label=wrong')))

    def test_only_aslr_module_base_is_excluded(self):
        b=copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows']if r['event']=='module')['base']='0x10000000'
        self.assertEqual(V.validate_runtime(b,self.spec)['admissions'],32)
        self.mutate('query-result','result',1)

if __name__=='__main__':unittest.main()
