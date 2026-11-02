import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_alternate364 as v
class Alternate364Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
    def reject(self,change):
        bundle=copy.deepcopy(self.bundle);change(bundle['cases'])
        with self.assertRaises(ValueError):v.runtime(bundle,self.spec)
    def test_complete(self):self.assertEqual(v.runtime(self.bundle,self.spec)['native_events'],1703)
    def test_missing_repeat(self):self.reject(lambda x:x.pop(1))
    def test_instrumented_control(self):self.reject(lambda x:x[2]['rows'].insert(1,dict(event='marker',seq=1)))
    def test_failed_execution(self):self.reject(lambda x:x[0]['rows'].insert(1,dict(event='trace-failed')))
    def test_wrong_binary(self):self.reject(lambda x:x[0]['rows'][0].update(sha256='bad'))
    def test_wrong_map(self):self.reject(lambda x:x[0]['rows'][0]['source_sha256'].update(map='bad'))
    def test_missing_completion(self):self.reject(lambda x:x[0]['rows'].pop())
    def test_changed_unhooked_behavior(self):self.reject(lambda x:x[2].update(preload=x[2]['preload'].replace('level=1','level=0',1)))
    def test_changed_category(self):
        def mutate(x):next(r for r in x[0]['rows']if r.get('event')=='submerge-category')['hide']^=1
        self.reject(mutate)
    def test_changed_initial_ethereal_state(self):
        def mutate(x):next(r for r in x[0]['rows']if r.get('event')=='counter158-release-leave')['after']['counter158']=1
        self.reject(mutate)
    def test_missing_initial_branch(self):
        def mutate(x):x[0]['rows'].pop(next(i for i,r in enumerate(x[0]['rows'])if r.get('event')=='initial-alternate-enter'))
        self.reject(mutate)
    def test_invented_initial_hold(self):
        def mutate(x):next(r for r in x[0]['rows']if r.get('event')=='morph-begin-leave')['after']['disableDepth']=1
        self.reject(mutate)
    def test_missing_positive_lifetime_repeat(self):
        bundle=copy.deepcopy(self.bundle);bundle['lifetime_cases'].pop(1)
        with self.assertRaises(ValueError):v.runtime(bundle,self.spec)
    def test_changed_positive_initial_deadline(self):
        bundle=copy.deepcopy(self.bundle)
        next(r for r in bundle['lifetime_cases'][0]['rows']if r.get('event')=='transition-timer')['delay']^=1
        with self.assertRaises(ValueError):v.runtime(bundle,self.spec)
if __name__=='__main__':unittest.main()
