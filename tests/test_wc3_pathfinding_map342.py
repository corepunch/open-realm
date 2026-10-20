import copy
import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_map342 as oracle
class RegionScopeEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.spec=json.loads(oracle.SPEC.read_text())
        self.bundle=json.loads(gzip.decompress(oracle.BUNDLE.read_bytes()))
    def test_frozen_repeats_and_control(self):
        self.assertEqual(oracle.validate_runtime(self.bundle,self.spec)['scenes'],8)
        oracle.validate(self.spec)
    def reject(self):
        with self.assertRaises(ValueError): oracle.validate_runtime(self.bundle,self.spec)
    def test_missing_event(self):
        self.bundle[0]['rows'].pop(10);self.reject()
    def test_wrong_counter(self):
        row=next(r for r in self.bundle[0]['rows'] if r.get('event')=='marker' and 'label=filter-enter' in r['value'])
        row['held']['counter']=2;self.reject()
    def test_changed_identity(self):
        row=next(r for r in self.bundle[0]['rows'] if 'identity' in r)
        row['identity'][0]+=1;self.reject()
    def test_changed_pointer_within_scene(self):
        row=next(r for r in self.bundle[0]['rows'] if r.get('event')=='unit-stop-exit')
        row['unit']='0x12345678';self.reject()
    def test_instrumented_control(self):
        self.bundle[2]['rows'].insert(1,{'event':'module'});self.reject()
    def test_unbalanced_scope(self):
        next(r for r in self.bundle[0]['rows'] if r.get('event')=='trace-end')['depth']=1;self.reject()
    def test_changed_public_file(self):
        self.bundle[2]['preload']+=' ';self.reject()
if __name__=='__main__':unittest.main()
