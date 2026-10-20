import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_morph374 as v

class Morph374Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
    def test_complete_native_contract(self):
        result=v.runtime(self.bundle,self.spec)
        for key,value in dict(settings=3,captures=9,scenes=18,public_markers=1320,native_events=1494,
                              recorded_system_exceptions=6,projected_events=2820,retained_diagnostics=4).items():
            self.assertEqual(result[key],value)
    def test_pins_and_complete_function_guards(self):self.assertEqual(v.validate(self.spec),self.bundle)
    def test_repeat_projection_and_literal_clear_ledgers(self):
        for setting,cases in self.bundle['settings'].items():
            for i,case in enumerate(cases[:2]):
                projected,ledger=v.project(v.rows(case))
                self.assertEqual(projected,self.spec['settings'][setting]['events'])
                self.assertEqual(ledger,self.spec['settings'][setting]['holder_ledgers'][i])
    def test_allocator_variance_is_preserved(self):
        self.assertEqual({s:d['whole_capture_allocator_variance_rows']for s,d in self.spec['settings'].items()},
                         {'default':0,'0':15,'1':23})
    def test_literal_words_and_identity(self):
        rows=v.rows(self.bundle['settings']['1'][0]);expected=v.projection(rows)
        for field in range(4):
            altered=copy.deepcopy(rows)
            state=next(r[k]for r in altered for k in('before','after','state')if isinstance(r.get(k),dict)and 'invisibilityScalar'in r[k])
            state['invisibilityScalar'][field]^=1
            self.assertNotEqual(v.projection(altered),expected)
        altered=copy.deepcopy(rows)
        for row in altered:
            for key in ('before','after','state'):
                if isinstance(row.get(key),dict)and 'identity'in row[key]:row[key]['identity'][0]^=0x10000
        self.assertNotEqual(v.projection(altered),expected)
        altered=copy.deepcopy(rows)
        row=next(r for r in altered if r.get('event')=='marker');row['tick']+=1
        self.assertNotEqual(v.projection(altered),expected)
    def test_public_and_exceptions_remain_literal(self):
        rows=v.rows(self.bundle['settings']['1'][0]);expected=v.projection(rows)
        for event,field in [('marker','value'),('native-exception','address')]:
            altered=copy.deepcopy(rows);next(r for r in altered if r.get('event')==event)[field]='changed'
            self.assertNotEqual(v.projection(altered),expected)
    def test_raw_capture_and_diagnostic_bytes_pinned(self):
        for field in ('settings','diagnostics'):
            b=copy.deepcopy(self.bundle)
            case=b[field]['0'][0]if field=='settings'else b[field][0]
            case['raw_jsonl']+='\n'
            with self.assertRaises(ValueError):v.runtime(b,self.spec)
    def test_missing_control_and_setting(self):
        for mutate in (lambda b:b['settings']['0'].pop(),lambda b:b['settings'].pop('default')):
            b=copy.deepcopy(self.bundle);mutate(b)
            with self.assertRaises(ValueError):v.runtime(b,self.spec)
    def test_exact_offsets_and_guards(self):
        for key in ('literal_unit_offsets','pointer_unit_offsets'):
            s=copy.deepcopy(self.spec);s['projection_contract'][key][0]+=4
            with self.assertRaises(ValueError):v.validate(s)
        for site in ('6f3e3c1d','6f670d54','6f3f4c70'):
            s=copy.deepcopy(self.spec);s['instructions'].pop(site)
            with self.assertRaises(ValueError):v.validate(s)
        s=copy.deepcopy(self.spec);s['vtable_slots'].pop('6fb67528')
        with self.assertRaises(ValueError):v.validate(s)
    def test_semantic_frontiers_reject_tampering(self):
        rs=v.rows(self.bundle['settings']['1'][0]);native=[r for r in rs if 'seq'in r];marks=v.public(self.bundle['settings']['1'][0]['preload'])
        for event,field,value in [('morph-policy-leave','result',0),('transition-timer','delay',0)]:
            changed=copy.deepcopy(native);next(r for r in changed if r['event']==event)[field]=value
            with self.assertRaises(ValueError):v.semantics(changed,marks,'1')
        changed=copy.deepcopy(native)
        next(r for r in changed if r['event']=='morph-form-disable-enter')['state']['invisibilityScalar'][4]=123
        with self.assertRaises(ValueError):v.semantics(changed,marks,'1')
        changed=marks.copy();i=next(i for i,m in enumerate(changed)if 'label=pre-forward-visibility 'in m)
        changed[i]=changed[i].replace('invisible=1','invisible=0')
        with self.assertRaises(ValueError):v.semantics(native,changed,'1')

class HolderLifetimeTests(unittest.TestCase):
    def row(self,seq,unit,a,b):
        return dict(seq=seq,tick=seq,event='state',state=dict(identity=[unit,1],invisibilityScalar=[1,2,3,4,a,b]))
    def test_concurrent_alias_and_last_holder_boundary(self):
        rows=[self.row(1,1,100,100),self.row(2,2,100,0),self.row(3,1,0,0),
              self.row(4,2,0,0),self.row(5,1,100,0),self.row(6,1,0,0)]
        projected,ledger=v.project(rows)
        self.assertEqual([x['seq']for x in ledger],[4,6])
        self.assertEqual([x['token']for x in ledger],[1,2])
        self.assertEqual(projected[0]['state']['invisibilityScalar'][4:],[['scalar-owner-lifetime',1]]*2)
        self.assertEqual(projected[1]['state']['invisibilityScalar'][4],['scalar-owner-lifetime',1])
        self.assertEqual(projected[4]['state']['invisibilityScalar'][4],['scalar-owner-lifetime',2])
    def test_address_relocation_with_lifetimes(self):
        a=[self.row(1,1,100,0),self.row(2,1,0,0),self.row(3,1,100,0),self.row(4,1,0,0)]
        b=[self.row(1,1,200,0),self.row(2,1,0,0),self.row(3,1,300,0),self.row(4,1,0,0)]
        self.assertEqual(v.projection(a),v.projection(b))
    def test_nonzero_replacement_rejected(self):
        with self.assertRaises(ValueError):v.project([self.row(1,1,100,0),self.row(2,1,200,0),self.row(3,1,0,0)])
    def test_missing_final_clear_rejected(self):
        with self.assertRaises(ValueError):v.project([self.row(1,1,100,0)])
    def test_early_zero_changes_contract(self):
        a=[self.row(1,1,100,0),self.row(2,1,100,0),self.row(3,1,0,0)]
        b=copy.deepcopy(a);b[1]['state']['invisibilityScalar'][4]=0
        self.assertNotEqual(v.project(a),v.project(b))
    def test_live_alias_cannot_become_new_token(self):
        a=[self.row(1,1,100,0),self.row(2,2,100,0),self.row(3,1,0,0),self.row(4,2,0,0)]
        b=copy.deepcopy(a);b[1]['state']['invisibilityScalar'][4]=200
        self.assertNotEqual(v.projection(a),v.projection(b))

if __name__=='__main__':unittest.main()
