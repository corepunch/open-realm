import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work248 as v

class CanonicalPointRequests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.FIXTURE.read_text())
        cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def test_complete_repeated_scopes(self):
        self.assertEqual(v.validate_runtime(self.bundle,self.spec),dict(captures=2,public_markers=494,scope_boundaries=48))

    def test_rejects_missing_pending_hold(self):
        events=copy.deepcopy(self.spec['events'])
        row=next(r for r in events if r['event']=='scope'and r['phase']=='acquire-leave'and len(r['rows'])==2)
        next(r for r in row['rows']if not r['ready'])['counter']-=1
        with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_rejects_lost_release(self):
        events=copy.deepcopy(self.spec['events']);events.remove(next(r for r in events if r['event']=='scope'and r['phase']=='release-enter'))
        with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_rejects_early_physical_binding(self):
        events=copy.deepcopy(self.spec['events'])
        row=next(r for r in events if r['event']=='scope'and r['phase']=='release-leave'and len(r['rows'])==2)
        next(r for r in row['rows']if r['ready'])['bound']=True
        with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_rejects_swapped_callback_and_readiness(self):
        events=copy.deepcopy(self.spec['events'])
        i=next(i for i,r in enumerate(events)if r==dict(event='issued',unit=1))
        j=next(i for i,r in enumerate(events)if r.get('event')=='ready'and r['unit']==1)
        events[i],events[j]=events[j],events[i]
        with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_native_control_and_literal_expectations(self):
        v.native_controls(self.bundle['original'],self.bundle['controls'])
        self.assertEqual(v.header(self.bundle['original']),v.HEADER.read_text())
        changed=copy.deepcopy(self.bundle['controls']);changed['rows'][0]['groups']=[[1,0,2]]
        with self.assertRaises(ValueError):v.native_controls(self.bundle['original'],changed)

    def test_rejects_incomplete_or_wrong_map(self):
        for field,value in [('complete',False),('markers',246)]:
            changed=copy.deepcopy(self.bundle);changed['captures'][0]['rows'][-1][field]=value
            with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)
        changed=copy.deepcopy(self.bundle);changed['map_metadata']['base_sha256']='0'*64
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)

if __name__=='__main__':unittest.main()
