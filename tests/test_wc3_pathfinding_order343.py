import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_order343 as oracle

class DecayEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.spec=json.loads(oracle.SPEC.read_text())
        self.bundle=json.loads(gzip.decompress(oracle.BUNDLE.read_bytes()))
    def reject(self):
        with self.assertRaises(ValueError):oracle.validate_runtime(self.bundle,self.spec)
    def test_complete_repeats_and_control(self):
        self.assertEqual(oracle.validate(self.spec)['units'],9)
    def test_missing_native_event(self):
        self.bundle[0]['rows'].pop(12);self.reject()
    def test_changed_scale(self):
        row=next(r for r in self.bundle[0]['rows'] if r.get('event')=='wait-enter' and r['scale'])
        row['scale']=0;self.reject()
    def test_instrumented_control(self):
        self.bundle[2]['rows'].insert(1,{'event':'module'});self.reject()
    def test_changed_public_file(self):
        self.bundle[2]['preload']+=' ';self.reject()
    def test_unowned_capture(self):
        self.bundle[0]['rows'][0]['owned']=False;self.reject()
    def test_wrong_native_identity(self):
        next(r for r in self.bundle[0]['rows'] if 'unit' in r)['unit']['identity'][0]+=1
        self.reject()
    def test_missing_cancellation_even_if_repeats_agree(self):
        # Semantic checks must reject a consistently changed golden, too.
        for r in self.spec['native']:
            if r['event']=='restore-scale-enter' and r['caller']=='0x67e14e':
                r['unit']['scaleFlags'] &= ~64
        for capture in self.bundle[:2]:
            replacement=iter(copy.deepcopy(self.spec['native']))
            capture['rows']=[next(replacement) if 'seq' in r and r['event']!='module' else r
                             for r in capture['rows']]
        self.reject()
if __name__=='__main__':unittest.main()
