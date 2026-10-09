"""Target-loss evidence preserves public heads, internal tasks and controls."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_target_reissue216 as V

class TargetReissueContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(V.FIXTURE.read_text())
        cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))

    def reject(self,change):
        b=copy.deepcopy(self.bundle);change(b)
        with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)

    def mutate(self,event,field,value):
        self.reject(lambda b:next(r for r in b['captures'][0]['rows'] if r['event']==event).__setitem__(field,value))

    def test_original_repeats_and_unhooked_control(self):
        self.assertEqual(V.validate_runtime(self.bundle,V.validate(self.spec)),
            dict(captures=2,controls=1,queries=340,losses=12,replacements=4,public_markers=105))

    def test_every_source_is_pinned(self):
        for path in V.SOURCES:
            spec=copy.deepcopy(self.spec);spec['pins'][path]='0'*64
            with self.assertRaises(ValueError):V.validate(spec)

    def test_public_head_and_internal_task_are_independent(self):
        for field,value in [('head',dict(command=851986,point=[0x44dc0000,0x44800000])),
                            ('task',dict(code=852339)),('caller','wrong'),('targetFlags',[0,0])]:
            self.mutate('lost',field,value)

    def test_reissue_preserves_original_point_and_command(self):
        for field,value in [('command',851986),('point',[0x44dc0000,0x44a00000]),
                            ('target',123),('tail',[0,0,1]),('player',0),('caller','wrong')]:
            self.mutate('replacement',field,value)

    def test_visibility_flags_and_invisibility_are_exact(self):
        for field in ('flags','unitFlags'):
            self.reject(lambda b:next(r for r in b['captures'][0]['rows'] if r['event']=='world-query').__setitem__(field,1))

    def test_each_repeat_and_control_is_required(self):
        for i in range(3):self.reject(lambda b:b['captures'].pop(i))

    def test_instrumented_controls_are_rejected(self):
        self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))

    def test_observer_failure_and_missing_completion_are_rejected(self):
        self.mutate('trace-end','readOnly',False)
        self.reject(lambda b:b['captures'][0]['rows'].insert(1,dict(event='trace-failed')))
        self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))

    def test_provenance_and_owned_process_are_required(self):
        self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
        self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].update(map='0'*64))
        self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].__setitem__('target216_observer.js','0'*64))

    def test_missing_or_extra_replacement_is_rejected(self):
        self.reject(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows'] if r['event']=='replacement')))
        self.reject(lambda b:b['captures'][0]['rows'].insert(2,next(r for r in b['captures'][0]['rows'] if r['event']=='replacement')))

    def test_all_public_positions_and_control_markers_are_required(self):
        for i in range(3):
            self.reject(lambda b:b['captures'][i].__setitem__('preload',b['captures'][i]['preload'].replace('label=ordered','label=wrong')))

if __name__=='__main__':unittest.main()
