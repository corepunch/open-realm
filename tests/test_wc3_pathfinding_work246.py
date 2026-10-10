import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work246 as v

class TargetFineRetention(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.FIXTURE.read_text())
        cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def test_original_repeat_and_control(self):
        self.assertEqual(v.validate_runtime(self.bundle,self.spec),dict(captures=2,controls=1,
            public_markers=222,route_samples=900,loss_witnesses=26,fine_samples=15))

    def test_world_projection_loses_original_words(self):
        rows=v.samples(self.bundle['captures'][0]['rows'])
        self.assertEqual(sum(any(v.roundtrip(x)!=x for x in r)for r in rows),13)

    def test_rejects_changed_sample_before_engine(self):
        changed=copy.deepcopy(self.bundle)
        row=next(r for r in changed['captures'][0]['rows']if r.get('event')=='sample')
        row['dest'][0]^=1
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)

    def test_rejects_hooked_control(self):
        changed=copy.deepcopy(self.bundle)
        changed['captures'][2]['rows'].insert(1,{'event':'trace-end','readOnly':True})
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)

    def test_rejects_incomplete_capture_and_wrong_map(self):
        for key,value in [('complete',False),('markers',73)]:
            changed=copy.deepcopy(self.bundle);changed['captures'][0]['rows'][-1][key]=value
            with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)
        changed=copy.deepcopy(self.bundle);changed['map_metadata']['fine_origin']=[0,0]
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)

    def test_original_header_is_reproducible(self):
        self.assertEqual(v.header(self.bundle['captures'][0]['rows']),v.HEADER.read_text())

if __name__=='__main__':unittest.main()
