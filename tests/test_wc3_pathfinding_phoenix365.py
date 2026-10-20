import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_phoenix365 as v
class Phoenix365Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
    def reject(self,change,kind='stock'):
        bundle=copy.deepcopy(self.bundle);change(bundle[kind])
        with self.assertRaises(ValueError):v.runtime(bundle,self.spec)
    def test_complete(self):
        self.assertEqual(v.runtime(self.bundle,self.spec),dict(captures=6,scenes=6,public_markers=1204,native_events=1618))
    def test_missing_repeat(self):self.reject(lambda x:x.pop(1))
    def test_missing_life_repeat(self):self.reject(lambda x:x.pop(1),'life')
    def test_instrumented_control(self):self.reject(lambda x:x[2]['rows'].insert(1,dict(event='marker',seq=1)))
    def test_failed_execution(self):self.reject(lambda x:x[0]['rows'].insert(1,dict(event='trace-failed')))
    def test_wrong_binary(self):self.reject(lambda x:x[0]['rows'][0].update(sha256='bad'))
    def test_wrong_map(self):self.reject(lambda x:x[0]['rows'][0]['source_sha256'].update(map='bad'))
    def test_missing_completion(self):self.reject(lambda x:x[0]['rows'].pop())
    def test_changed_unhooked_behavior(self):self.reject(lambda x:x[2].update(preload=x[2]['preload'].replace('P365 ','P365 changed ',1)))
    def test_changed_life_behavior(self):self.reject(lambda x:x[2].update(preload=x[2]['preload'].replace('P365 ','P365 changed ',1)),'life')
    def test_changed_original_event(self):
        def mutate(x):
            row=next(r for r in x[0]['rows']if 'seq'in r and r.get('event')!='module');row['tick']=row.get('tick',0)+1
        self.reject(mutate)
    def test_changed_observer_counts(self):
        def mutate(x):next(r for r in x[0]['rows']if r.get('event')=='trace-end')['counts']['marker']+=1
        self.reject(mutate)
    def test_missing_native_event(self):
        def mutate(x):x[0]['rows'].pop(next(i for i,r in enumerate(x[0]['rows'])if 'seq'in r and r.get('event')!='module'))
        self.reject(mutate)
    def test_incomplete_life_control(self):self.reject(lambda x:x[2]['rows'][-1].update(complete=False),'life')
if __name__=='__main__':unittest.main()
