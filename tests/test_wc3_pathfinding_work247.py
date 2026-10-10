import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work247 as v

class StopBridgeScope(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.FIXTURE.read_text())
        cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def test_repeats_and_control(self):
        self.assertEqual(v.validate_runtime(self.bundle,self.spec),dict(captures=2,controls=1,
            public_markers=42,bridge_scopes=16,placement_searches=6,commits=2))

    def test_rejects_counter_and_mode_changes(self):
        for key in('counter','mode'):
            changed=copy.deepcopy(self.bundle)
            row=next(r for r in changed['captures'][0]['rows']if r.get('event')=='footprint')
            row[key]^=1
            with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)

    def test_rejects_missing_release(self):
        changed=copy.deepcopy(self.bundle)
        row=next(r for r in changed['captures'][0]['rows']if r.get('event')=='outer-toggle'and r['on']==0)
        changed['captures'][0]['rows'].remove(row)
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)

    def test_rejects_hooked_control(self):
        changed=copy.deepcopy(self.bundle);changed['captures'][2]['rows'].insert(1,{'event':'trace-end'})
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)

    def test_rejects_incomplete_capture_or_wrong_map(self):
        changed=copy.deepcopy(self.bundle);changed['captures'][0]['rows'][-1]['complete']=False
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)
        changed=copy.deepcopy(self.bundle);changed['map_metadata']['fine_origin']=[-8192,0]
        with self.assertRaises(ValueError):v.validate_runtime(changed,self.spec)

    def test_original_fixture_and_observer_free_finals(self):
        original=self.bundle['original']
        self.assertEqual(len(original['rows']),32)
        self.assertEqual(v.header(original),v.HEADER.read_text())
        self.assertEqual([{k:x for k,x in r.items()if k!='trace'}for r in original['rows']],original['controls'])
        for r in original['rows']:
            inner=[x for x in r['trace']if x[0]==0x149370]
            self.assertEqual(len(inner)>0,r['kind']!='no_callback')
            for x in inner:self.assertEqual(x[1:],[r['outer']+2,1])
            self.assertEqual(r['counter'],r['outer'])
            self.assertEqual(r['mode'],7)

if __name__=='__main__':unittest.main()
