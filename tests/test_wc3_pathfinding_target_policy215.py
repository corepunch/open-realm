"""Retail visibility evidence must retain producers, flag policy and controls."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_target_policy215 as V

class TargetPolicyContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(V.FIXTURE.read_text())
        cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))

    def reject(self,change):
        b=copy.deepcopy(self.bundle);change(b)
        with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)

    def test_original_repeats_and_unhooked_control(self):
        self.assertEqual(V.validate_runtime(self.bundle,V.validate(self.spec)),
                         dict(captures=2,controls=1,queries=66,toggles=4,public_markers=63))

    def test_every_source_is_pinned(self):
        for path in V.SOURCES:
            spec=copy.deepcopy(self.spec);spec['pins'][path]='0'*64
            with self.assertRaises(ValueError):V.validate(spec)

    def test_query_flags_mode_owner_and_invisibility_are_exact(self):
        for field in ('flags','mode','player','unitFlags'):
            def change(b):
                row=next(r for r in b['captures'][0]['rows'] if r['event']=='world-query')
                row[field]^=1
            self.reject(change)

    def test_fog_setter_enable_and_caller_are_exact(self):
        for field,value in [('enabled',1),('caller','1fe5c4')]:
            def change(b):
                next(r for r in b['captures'][0]['rows'] if r['event']=='fog-setter')[field]=value
            self.reject(change)

    def test_showmap_must_be_synchronized(self):
        self.reject(lambda b:next(r for r in b['captures'][0]['rows'] if r['event']=='show-map').update(caller='919123'))

    def test_instrumented_controls_are_rejected(self):
        self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))

    def test_each_repeat_and_control_is_required(self):
        for i in range(3):self.reject(lambda b:b['captures'].pop(i))

    def test_observer_failure_and_missing_completion_are_rejected(self):
        self.reject(lambda b:next(r for r in b['captures'][0]['rows'] if r['event']=='trace-end').update(readOnly=False))
        self.reject(lambda b:b['captures'][0]['rows'].insert(1,dict(event='trace-failed')))
        self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))

    def test_provenance_and_owned_process_are_required(self):
        self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
        self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].update(map='0'*64))
        self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].update(target215_observer_js='bad',**{'target215_observer.js':'0'*64}))

    def test_diagnostic_reissue_and_zero_reveal_are_preserved(self):
        self.reject(lambda b:next(r for r in b['captures'][0]['rows'] if r['event']=='lost').update(caller='wrong'))
        self.reject(lambda b:next(r for r in b['captures'][0]['rows'] if r['event']=='reveal')['masks'].__setitem__(0,8))

if __name__=='__main__':unittest.main()
