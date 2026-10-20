import gzip
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_foot347 as oracle
class RadiusProducerEvidenceTests(unittest.TestCase):
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
        self.assertEqual(len(result['radii']),253)
        self.assertEqual(len(result['public_rows']),1209)
        oracle.check_header(result)
    def test_unowned_control(self):
        self.change(['control-o.jsonl'],lambda r:r['event']=='metadata',lambda r:r.update(owned=False));self.reject()
    def test_native_repeat_disagreement(self):
        self.change(['repeat-q.jsonl'],lambda r:r['event']=='public-position'and r['tick']==35,lambda r:r['xy'].__setitem__(0,r['xy'][0]+1));self.reject()
    def test_cached_hull_cannot_follow_the_live_maximum(self):
        self.change(['observe-p.jsonl','repeat-q.jsonl'],lambda r:r['event']=='commit-enter'and r['tick']>=40 and r['g']['shared'],lambda r:r['g'].update(cached=r['g']['shared']['radius']));self.reject()
    def test_largest_resize_must_reach_both_bound_groups(self):
        self.change(['observe-p.jsonl','repeat-q.jsonl'],lambda r:r['event']=='commit-enter'and r['tick']==30 and r['g']['shared'],lambda r:r['g']['shared'].update(radius=0x403e0000));self.reject()
    def test_final_getter_window_is_not_stale(self):
        self.change(['observe-p.jsonl','repeat-q.jsonl'],lambda r:r['event']=='public-position'and r['tick']==100 and r['u']==0,lambda r:r.update(xy=[0x44038d2b,0x44b8ecc5]));self.reject()
    def test_complete_target_caller_inventory(self):
        self.spec['target_callers'].pop();self.reject()
    def test_archive_pin(self):
        self.files['control-o-preload.txt']+=' ';self.reject()
if __name__=='__main__':unittest.main()
