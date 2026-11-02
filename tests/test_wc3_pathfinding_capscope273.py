import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_capscope273 as v

class CanonicalCaptainScope(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.FIXTURE.read_text())
        cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def test_repeated_captain_and_unhooked_control(self):
        self.assertEqual(v.validate_runtime(self.bundle,self.spec),dict(captures=2,controls=1,public_markers=182,publications=17,targeted=3,scope_boundaries=68,target_boundaries=68))

    def test_literal_original_header_and_controls(self):
        v.native_controls(self.bundle['original'],self.bundle['control'])
        self.assertEqual(v.header(self.bundle['original']),v.HEADER.read_text())
        changed=copy.deepcopy(self.bundle['control']);changed['rows'][0]['events'][0]['state']['counters'][0]+=1
        with self.assertRaises(ValueError):v.native_controls(self.bundle['original'],changed)

    def test_rejects_target_hold_after_members(self):
        events=copy.deepcopy(self.spec['events'])
        i=next(i for i,r in enumerate(events)if r['event']=='target-toggle'and r['target']and r['on']==1 and r['phase']=='leave')
        j=next(j for j in range(i+1,len(events))if events[j]['event']=='scope')
        events[i],events[j]=events[j],events[i]
        with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_rejects_target_release_before_members(self):
        events=copy.deepcopy(self.spec['events'])
        row=next(r for r in events if r['event']=='target-toggle'and r['target']and r['on']==0 and r['phase']=='enter')
        row['target']['counter']-=1
        with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_rejects_missing_pending_or_target_hold(self):
        for owner in ('rows','target'):
            events=copy.deepcopy(self.spec['events'])
            row=next(r for r in events if r['event']=='scope'and r['target']and r['phase']=='acquire-leave')
            (row[owner][0]['record']if owner=='rows'else row[owner])['counter']-=1
            with self.assertRaises(ValueError):v.verify_scopes(events)

    def test_rejects_incomplete_capture_and_perturbed_public_motion(self):
        changed=copy.deepcopy(self.bundle);changed['captures'][0]['rows'][-1]['complete']=False
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)
        changed=copy.deepcopy(self.bundle);changed['captures'][2]['preload']=changed['captures'][2]['preload'].replace('unit=0','unit=9',1)
        self.assertNotEqual(changed['captures'][2]['preload'],self.bundle['captures'][2]['preload'])
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)

if __name__=='__main__':unittest.main()
