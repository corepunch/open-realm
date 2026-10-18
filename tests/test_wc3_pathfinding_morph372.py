import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_morph372 as v

class Morph372Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
    def reject(self,change):
        bundle=copy.deepcopy(self.bundle);change(bundle)
        with self.assertRaises(ValueError):v.runtime(bundle,self.spec)
    def row(self,bundle,event='ability-disable-add-enter'):
        return next(r for r in bundle['cases'][0]['rows']if r.get('event')==event)
    def scalar(self,bundle):
        return next(r[k]['invisibilityScalar']for r in bundle['cases'][0]['rows']for k in('before','after','state')
                    if isinstance(r.get(k),dict)and 'invisibilityScalar'in r[k]and r[k]['invisibilityScalar'][4])
    def test_complete(self):
        self.assertEqual(v.runtime(self.bundle,self.spec),dict(captures=3,scenes=5,public_markers=311,
            native_events=449,recorded_system_exceptions=2,projected_events=762))
    def test_bundle_sources_and_static_inventory(self):self.assertEqual(v.validate(self.spec),self.bundle)
    def test_missing_repeat(self):self.reject(lambda b:b['cases'].pop(1))
    def test_failed_execution(self):self.reject(lambda b:b['cases'][0]['rows'].insert(1,dict(event='trace-failed')))
    def test_unknown_row(self):self.reject(lambda b:b['cases'][0]['rows'].insert(1,dict(event='new-native-hook')))
    def test_instrumented_control(self):self.reject(lambda b:b['cases'][2]['rows'].insert(1,dict(event='marker',seq=1)))
    def test_exception_in_control(self):self.reject(lambda b:b['cases'][2]['rows'].insert(1,dict(event='native-exception')))
    def test_wrong_binary(self):self.reject(lambda b:b['cases'][0]['rows'][0].update(sha256='bad'))
    def test_wrong_pe(self):self.reject(lambda b:b['cases'][0]['rows'][0].update(timestamp=0))
    def test_wrong_map(self):self.reject(lambda b:b['cases'][0]['rows'][0]['source_sha256'].update(map='bad'))
    def test_controller_mutation(self):self.reject(lambda b:b['controllers'].update({'queue263-ui.exe':'Zm9v'}))
    def test_missing_controller(self):self.reject(lambda b:b['controllers'].pop('queue263-ui.exe.so'))
    def test_missing_completion(self):self.reject(lambda b:b['cases'][0]['rows'].pop())
    def test_changed_control(self):self.reject(lambda b:b['cases'][2].update(preload=b['cases'][2]['preload'].replace('M372 ','M372 changed ',1)))
    def test_readonly_claim(self):
        self.reject(lambda b:next(r for r in b['cases'][0]['rows']if r.get('event')=='trace-end').update(readOnly=False))
    def test_literal_sequence(self):self.reject(lambda b:self.row(b).__setitem__('seq',999))
    def test_literal_primary_clock(self):self.reject(lambda b:self.row(b)['primary'].__setitem__(0,0))
    def test_literal_unit_identity(self):self.reject(lambda b:self.row(b)['state']['identity'].__setitem__(0,123))
    def test_literal_scalar_words(self):
        for i in range(4):
            with self.subTest(word=i):self.reject(lambda b:self.scalar(b).__setitem__(i,0x12345678))
    def test_scalar_width(self):self.reject(lambda b:self.scalar(b).append(0))
    def test_scalar_pointer_alias(self):
        def mutate(b):
            words=self.scalar(b);self.assertNotEqual(words[4],words[5]);words[5]=words[4]
        self.reject(mutate)
    def test_scalar_null(self):self.reject(lambda b:self.scalar(b).__setitem__(5,0))
    def test_consistent_scalar_heap_relocation(self):
        b=copy.deepcopy(self.bundle)
        for case in b['cases'][:2]:
            for r in case['rows']:
                for key in('before','after','state'):
                    if isinstance(r.get(key),dict)and 'invisibilityScalar'in r[key]:
                        for i in(4,5):
                            words=r[key]['invisibilityScalar']
                            if words[i]not in(0,0xffffffff):words[i]+=0x100000
        self.assertEqual(v.runtime(b,self.spec)['native_events'],449)
    def test_availability_literals(self):
        for key in('class','word50','flags','execution','orders'):
            with self.subTest(field=key):self.reject(lambda b:self.row(b)['availability'].__setitem__(key,'changed'))
    def test_preserved_system_exception(self):
        self.reject(lambda b:self.row(b,'native-exception')['trace'].__setitem__(0,'changed'))
    def test_missing_system_exception(self):
        self.reject(lambda b:b['cases'][0]['rows'].remove(self.row(b,'native-exception')))
    def test_missing_guard(self):
        spec=copy.deepcopy(self.spec);spec['instructions'].pop(next(iter(spec['instructions'])))
        with self.assertRaises(ValueError):v.validate(spec)
    def test_exact_projection_offsets(self):
        for key in('pointer_unit_offsets','literal_unit_offsets'):
            spec=copy.deepcopy(self.spec);spec['projection_contract'][key][0]+=4
            with self.subTest(field=key),self.assertRaises(ValueError):v.validate(spec)
    def test_missing_source_pin(self):
        spec=copy.deepcopy(self.spec);spec['pins'].pop(next(iter(spec['pins'])))
        with self.assertRaises(ValueError):v.validate(spec)

if __name__=='__main__':unittest.main()
