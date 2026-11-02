import copy,gzip,importlib.util,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_ethereal357 as v
class Ethereal357Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
    def reject(self,change):
        cases=copy.deepcopy(self.bundle['cases']);change(cases)
        with self.assertRaises(ValueError):v.runtime(cases,self.spec)
    def test_complete_fixture(self):self.assertEqual(v.runtime(self.bundle['cases'],self.spec)['native_events'],894)
    def test_missing_repeat(self):self.reject(lambda x:x.pop(1))
    def test_instrumented_control(self):self.reject(lambda x:x[2]['rows'].insert(1,dict(event='marker',seq=1)))
    def test_missing_completion(self):self.reject(lambda x:x[0]['rows'].pop())
    def test_controller_failure(self):self.reject(lambda x:x[0]['rows'].insert(1,dict(event='trace-failed')))
    def test_wrong_binary(self):self.reject(lambda x:x[0]['rows'][0].update(sha256='bad'))
    def test_unowned_process(self):self.reject(lambda x:x[0]['rows'][0].update(owned=False))
    def test_wrong_source(self):self.reject(lambda x:x[0]['rows'][0]['source_sha256'].update(map='bad'))
    def test_missing_native_row(self):
        def mutate(x):x[1]['rows'].pop(next(i for i,r in enumerate(x[1]['rows'])if r.get('event')=='morph-begin-enter'))
        self.reject(mutate)
    def test_changed_signed_owner(self):
        def mutate(x):next(r for r in x[0]['rows']if r.get('event')=='counter158-acquire-leave')['after']['counter158']=2
        self.reject(mutate)
    def test_changed_primary_deadline(self):
        def mutate(x):next(r for r in x[0]['rows']if r.get('event')=='transition-timer')['delay']^=1
        self.reject(mutate)
    def test_changed_disabled_configuration(self):
        def mutate(x):next(r for r in x[0]['rows']if r.get('event')=='configuration')['args'][0]^=1
        self.reject(mutate)
    def test_changed_unhooked_public_behavior(self):self.reject(lambda x:x[2].update(preload=x[2]['preload'].replace('paused=0','paused=1',1)))
    def test_positive_lifetime_fixture(self):
        self.assertEqual(v.lifetime_runtime(self.bundle['lifetime_cases'],self.spec)['native_events'],274)
    def test_positive_lifetime_requires_repeat(self):
        with self.assertRaises(ValueError):v.lifetime_runtime(self.bundle['lifetime_cases'][:1],self.spec)
    def test_positive_lifetime_rejects_changed_deadline(self):
        cases=copy.deepcopy(self.bundle['lifetime_cases'])
        next(r for r in cases[0]['rows']if r.get('event')=='transition-timer')['delay']^=1
        with self.assertRaises(ValueError):v.lifetime_runtime(cases,self.spec)
    def test_positive_lifetime_rejects_changed_unhooked_behavior(self):
        cases=copy.deepcopy(self.bundle['lifetime_cases']);cases[2]['preload']=cases[2]['preload'].replace('paused=0','paused=1',1)
        with self.assertRaises(ValueError):v.lifetime_runtime(cases,self.spec)
if __name__=='__main__':unittest.main()
