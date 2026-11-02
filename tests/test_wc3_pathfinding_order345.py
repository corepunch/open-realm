import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_order345 as oracle

class AttackFailureEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.spec=json.loads(oracle.SPEC.read_text())
        self.bundle=json.loads(gzip.decompress(oracle.BUNDLE.read_bytes()))
    def reject(self):
        with self.assertRaises(ValueError):oracle.validate_runtime(self.bundle,self.spec)
    def changed_golden(self,predicate,change):
        for row in self.spec['native']:
            if predicate(row):change(row)
        for capture in self.bundle[:2]:
            replacement=iter(copy.deepcopy(self.spec['native']))
            capture['rows']=[next(replacement) if 'seq' in r and r['event']!='module' else r for r in capture['rows']]
    def test_complete_repeats_and_control(self):
        self.assertEqual(oracle.validate(self.spec)['public_markers'],2277)
    def test_missing_native_event(self):
        self.bundle[0]['rows'].pop(12);self.reject()
    def test_instrumented_control(self):
        self.bundle[2]['rows'].insert(1,{'event':'module'});self.reject()
    def test_changed_public_file(self):
        self.bundle[2]['preload']+=' ';self.reject()
    def test_unowned_capture(self):
        self.bundle[0]['rows'][0]['owned']=False;self.reject()
    def test_wrong_native_identity(self):
        next(r for r in self.bundle[0]['rows'] if 'unit' in r)['unit']['identity'][0]+=1
        self.reject()
    def test_wrong_branch_even_if_repeats_agree(self):
        self.changed_golden(lambda r:r['event']=='cant-path-enter' and r['scene']==1,
                            lambda r:r.update(flags=r['flags']&~2))
        self.reject()
    def test_invulnerable_target_cannot_bind_attack_even_if_repeats_agree(self):
        self.changed_golden(lambda r:r['event']=='ai-append' and r['scene']==3 and r['caller']=='0x60328c',
                            lambda r:r['unit'].update(attackTarget=[908,1549]))
        self.reject()
    def test_vulnerable_target_must_bind_attack_even_if_repeats_agree(self):
        self.changed_golden(lambda r:r['event']=='ai-append' and r['scene']==2 and r['caller']=='0x60328c',
                            lambda r:r['unit'].update(attackTarget=[0xffffffff,0xffffffff]))
        self.reject()
    def test_acquisition_prevention_is_not_invulnerability(self):
        self.changed_golden(lambda r:r['event']=='alternative-enter',lambda r:r.update(invulnerable=1))
        self.reject()
    def test_nearest_distance_is_frozen(self):
        self.changed_golden(lambda r:r['event']=='alternative-leave',lambda r:r.update(best=r['best']+1))
        self.reject()
if __name__=='__main__':unittest.main()
