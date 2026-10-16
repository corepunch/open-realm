import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_power349 as oracle
class PowerCallerEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.spec=json.loads(oracle.SPEC.read_text())
        self.files=json.loads(gzip.decompress(oracle.BUNDLE.read_bytes()))
    def change(self,names,predicate,edit):
        for name in names:
            rows=[json.loads(l)for l in self.files[name].splitlines()]
            for row in rows:
                if predicate(row):edit(row)
            self.files[name]='\n'.join(json.dumps(r)for r in rows)+'\n'
            self.spec['files'][name]=oracle.digest(self.files[name].encode())
    def reject(self):
        with self.assertRaises(ValueError):oracle.validate(self.spec,self.files)
    def test_full_stream_and_engine_literals(self):
        result=oracle.validate(self.spec,self.files)
        self.assertEqual(len(result['public_rows']),422)
        self.assertEqual(result['scalar_calls'],1351)
        oracle.check_header(result)
    def test_unowned_control(self):
        self.change(['control-g.jsonl'],lambda r:r['event']=='metadata',lambda r:r.update(owned=False));self.reject()
    def test_changed_both_observers_cannot_rebaseline_getters(self):
        self.change(['observe-e.jsonl','repeat-f.jsonl'],lambda r:r['event']=='public-values'and r['tick']==17,lambda r:r['values'].__setitem__(4,0));self.reject()
    def test_changed_caller_alias(self):
        self.change(['observe-e.jsonl','repeat-f.jsonl'],lambda r:r['event']=='scalar-enter',lambda r:r.update(alias=True));self.reject()
    def test_missing_instruction_guard(self):
        self.spec['instructions'].popitem();self.reject()
    def test_elapsed_clock_is_independently_required(self):
        self.change(['observe-j.jsonl'],lambda r:r['event']=='curve-age',lambda r:r.update(value=0));self.reject()
    def test_health_commit_is_independently_required(self):
        self.change(['observe-j.jsonl'],lambda r:r['event']=='scalar-value-committed'and r['tick']==46,lambda r:r.update(base=0));self.reject()
    def test_archive_pin(self):
        self.files['control-g-preload.txt']+=' ';self.reject()
if __name__=='__main__':unittest.main()
