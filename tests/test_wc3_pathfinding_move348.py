import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_move348 as oracle
class ParameterOverwriteEvidenceTests(unittest.TestCase):
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
    def test_accepted_full_stream_and_engine_literals(self):
        result=oracle.validate(self.spec,self.files)
        self.assertEqual(len(result['public_rows']),488)
        self.assertEqual(result['writes'],152)
        oracle.check_header(result)
    def test_unowned_control(self):
        self.change(['control-c.jsonl'],lambda r:r['event']=='metadata',lambda r:r.update(owned=False));self.reject()
    def test_native_repeat_disagreement(self):
        self.change(['repeat-e.jsonl'],lambda r:r['event']=='public-values'and r['tick']==27,lambda r:r['values'].__setitem__(2,0));self.reject()
    def test_rebaseline_both_observers_cannot_change_publication(self):
        self.change(['observe-d.jsonl','repeat-e.jsonl'],lambda r:r['event']=='write-enter',lambda r:r.update(value=0));self.reject()
    def test_removed_intrinsic_factory_result(self):
        self.change(['observe-d.jsonl','repeat-e.jsonl'],lambda r:r['event']=='ability-result'and r['kind']=='add',lambda r:r.update(result=1));self.reject()
    def test_instruction_inventory_is_required(self):
        self.spec['instructions'].popitem();self.reject()
    def test_archive_pin(self):
        self.files['control-c-preload.txt']+=' ';self.reject()
if __name__=='__main__':unittest.main()
