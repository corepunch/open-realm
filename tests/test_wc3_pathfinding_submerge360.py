import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_submerge360 as v
class Submerge360Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
    def reject(self,change):
        bundle=copy.deepcopy(self.bundle);change(bundle)
        with self.assertRaises(ValueError):v.runtime(bundle,self.spec)
    def test_complete(self):self.assertEqual(v.runtime(self.bundle,self.spec)['native_events'],1468)
    def test_missing_stock_repeat(self):self.reject(lambda x:x['stock'].pop(1))
    def test_missing_zero_repeat(self):self.reject(lambda x:x['zero'].pop(1))
    def test_instrumented_control(self):self.reject(lambda x:x['stock'][2]['rows'].insert(1,dict(event='marker',seq=1)))
    def test_failed_execution(self):self.reject(lambda x:x['zero'][0]['rows'].insert(1,dict(event='trace-failed')))
    def test_wrong_binary(self):self.reject(lambda x:x['stock'][0]['rows'][0].update(sha256='bad'))
    def test_wrong_map(self):self.reject(lambda x:x['zero'][0]['rows'][0]['source_sha256'].update(map='bad'))
    def test_missing_completion(self):self.reject(lambda x:x['stock'][0]['rows'].pop())
    def test_changed_unhooked_behavior(self):self.reject(lambda x:x['zero'][2].update(preload=x['zero'][2]['preload'].replace('paused=0','paused=1',1)))
    def test_changed_category(self):
        def mutate(x):next(r for r in x['stock'][0]['rows']if r.get('event')=='submerge-category')['hide']^=1
        self.reject(mutate)
    def test_changed_zero_deadline(self):
        def mutate(x):next(r for r in x['zero'][0]['rows']if r.get('event')=='transition-timer')['delay']^=1
        self.reject(mutate)
    def test_missing_acquisition(self):
        def mutate(x):x['stock'][0]['rows'].pop(next(i for i,r in enumerate(x['stock'][0]['rows'])if r.get('event')=='acquire-enter'))
        self.reject(mutate)
if __name__=='__main__':unittest.main()
