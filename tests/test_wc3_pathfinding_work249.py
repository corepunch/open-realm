import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work249 as v

class QueuedCanonicalScopes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.FIXTURE.read_text())
        cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def test_complete_repeated_pending_and_success_scopes(self):
        self.assertEqual(v.validate_runtime(self.bundle,self.spec),dict(captures=2,pending_publications=2,source_publications=2,public_markers=610,scope_boundaries=16))

    def test_rejects_pending_source_without_exclusion(self):
        events=copy.deepcopy(self.spec['events']);events['scopes'][1]['rows'][0]['counter']=0
        with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_rejects_premature_inherited_owner_replacement(self):
        events=copy.deepcopy(self.spec['events']);events['scopes'][3]['rows'][1]['group']=1
        with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_rejects_readiness_before_complete_attachment(self):
        events=copy.deepcopy(self.spec['events']);events['scopes'][0]['rows'].pop()
        with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_rejects_missing_release_or_source_readiness(self):
        for index in(3,7):
            events=copy.deepcopy(self.spec['events']);events['scopes'].pop(index)
            with self.assertRaises(ValueError):v.verify_scopes(events)
        events=copy.deepcopy(self.spec['events']);events['scopes'][4]['rows'][0]['ready']=False
        with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_native_controls_old_owner_and_frozen_partitions(self):
        v.native_controls(self.bundle['original'],self.bundle['controls'])
        self.assertEqual(v.header(self.bundle['original']),v.HEADER.read_text())
        for field,value in [('old_retained',0),('old_prepared',2),('groups',[[1,0,2]])]:
            changed=copy.deepcopy(self.bundle['original']);changed['rows'][0][field]=value
            with self.assertRaises(ValueError):v.native_controls(changed,self.bundle['controls'])

    def test_rejects_truncated_or_wrong_map_capture(self):
        for field,value in [('complete',False),('markers',304)]:
            changed=copy.deepcopy(self.bundle);changed['captures'][0]['rows'][-1][field]=value
            with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)
        changed=copy.deepcopy(self.bundle);changed['captures'][0]['rows'][0]['source_sha256']['map']='0'*64
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)

if __name__=='__main__':unittest.main()
