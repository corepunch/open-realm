import base64,copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_morph390 as v
from test_wc3_pathfinding_morph374 import HolderLifetimeTests

class Morph390Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
    def test_complete_original_contract(self):
        self.assertEqual(v.runtime(self.bundle,self.spec),dict(captures=12,scenes=10,public_markers=746,
            projected_events=1788,native_events=1034,recorded_system_exceptions=8))
    def test_complete_guards_sources_and_bundle(self):self.assertEqual(v.validate(self.spec),self.bundle)
    def test_concrete_policy_detach_and_takeoff_slots(self):
        instructions,data=v.guards();self.assertEqual(len(instructions),4179);self.assertEqual(len(data),29)
        self.assertEqual(data['6fafef58'],'40c6546f');self.assertEqual(data['6fafebe0'],'3002506f');self.assertEqual(data['6fafef40'],'50cb546f')
        for field,key in [('instructions','6f54ff50'),('instructions','6f562c50'),('data_guards','6fafebe0'),('data_guards','6fd1b3a4')]:
            spec=copy.deepcopy(self.spec);spec[field].pop(key)
            with self.assertRaises(ValueError):v.validate(spec)
    def test_missing_repeat_control_or_setting(self):
        for key in ['ordinary0','detach1']:
            for capture in ['observe_b','control_a']:
                b=copy.deepcopy(self.bundle);b['groups'][key].pop(capture)
                with self.assertRaises(ValueError):v.runtime(b,self.spec)
        b=copy.deepcopy(self.bundle);b['groups'].pop('ordinary1')
        with self.assertRaises(ValueError):v.runtime(b,self.spec)
    def test_original_bytes_including_wrapper_rows_pinned(self):
        for key in ['capture','preload']:
            b=copy.deepcopy(self.bundle);case=b['groups']['ordinary0']['observe_a']
            case[key]=base64.b64encode(v.decode(case,key)+b'\n').decode()
            with self.assertRaises(ValueError):v.runtime(b,self.spec)
    def test_all_numeric_scalar_words_identity_and_clock_literal(self):
        rows=v.parse_rows(self.bundle['groups']['ordinary1']['observe_a']);expected=v.project(rows)[0]
        for field in range(4):
            changed=copy.deepcopy(rows);state=next(r[k]for r in changed for k in('state','before','after')if isinstance(r.get(k),dict)and 'invisibilityScalar'in r[k]);state['invisibilityScalar'][field]^=1
            self.assertNotEqual(v.project(changed)[0],expected)
        for key in ['identity','primary']:
            changed=copy.deepcopy(rows)
            if key=='primary':next(r for r in changed if isinstance(r.get('primary'),list))['primary'][0]^=1
            else:
                for r in changed:
                    for k in('state','before','after'):
                        if isinstance(r.get(k),dict)and 'identity'in r[k]:r[k]['identity'][0]^=0x10000
            self.assertNotEqual(v.project(changed)[0],expected)
    def test_public_and_system_exceptions_literal(self):
        rows=v.parse_rows(self.bundle['groups']['ordinary0']['observe_a']);expected=v.project(rows)[0]
        for event,field in [('marker','value'),('native-exception','address')]:
            changed=copy.deepcopy(rows);next(r for r in changed if r['event']==event)[field]='changed'
            self.assertNotEqual(v.project(changed)[0],expected)
    def test_exact_proven_pointer_offsets(self):
        for field in ['literal_unit_offsets','pointer_unit_offsets']:
            spec=copy.deepcopy(self.spec);spec['projection_contract'][field][0]+=4
            with self.assertRaises(ValueError):v.validate(spec)
    def test_literal_retirement_ledger(self):
        b=copy.deepcopy(self.bundle);b['groups']['detach1']['observe_a']['retirement_ledger'][0]['pointer']+=4
        with self.assertRaises(ValueError):v.runtime(b,self.spec)
    def test_policy_timers_and_rebind_frontiers(self):
        case=self.bundle['groups']['ordinary1']['observe_a'];rows=v.parse_rows(case);marks=v.public(case,'P390')
        for event,field,value in [('morph-policy-leave','result',0),('transition-timer','delay',v.TAKEOFF),('morph-form-disable-leave','tick',21),('morph-form-enable-leave','tick',38)]:
            changed=copy.deepcopy(rows);next(r for r in changed if r['event']==event)[field]=value
            with self.assertRaises(ValueError):v.semantic(changed,marks,1,False)
    def test_takeoff_release_must_precede_separation(self):
        case=self.bundle['groups']['ordinary1']['observe_a'];rows=v.parse_rows(case);marks=v.public(case,'P390')
        i=next(i for i,r in enumerate(rows)if r['event']=='morph-form-enable-leave')
        j=next(j for j in range(i,len(rows))if rows[j]['event']=='morph-begin-enter')
        changed=copy.deepcopy(rows);changed[i],changed[j]=changed[j],changed[i]
        with self.assertRaises(ValueError):v.semantic(changed,marks,1,False)
    def test_takeoff_fade_witness_is_required(self):
        case=self.bundle['groups']['ordinary1']['observe_a'];rows=v.parse_rows(case);marks=v.public(case,'P390')
        i=next(i for i,x in enumerate(marks)if ' tick=42 'in x and 'label=sample'in x);marks[i]=marks[i].replace('invisible=1','invisible=0')
        with self.assertRaises(ValueError):v.semantic(rows,marks,1,False)
    def test_enemy_positive_control_is_required(self):
        case=self.bundle['groups']['ordinary1']['observe_a'];rows=v.parse_rows(case);marks=v.public(case,'P390')
        i=next(i for i,x in enumerate(marks)if 'label=pre-forward-visibility 'in x);marks[i]=marks[i].replace('invisible=1','invisible=0')
        with self.assertRaises(ValueError):v.semantic(rows,marks,1,False)
    def test_concrete_detach_inverse_and_restart_fade(self):
        case=self.bundle['groups']['detach1']['observe_a'];rows=v.parse_rows(case);marks=v.public(case,'P390D')
        for field,value in [('rawcode',v.UGRM),('disableDepth',1),('invisibilityDepth',0),('flags',0x1000000)]:
            changed=copy.deepcopy(rows);next(r for r in changed if r['event']=='stone-detach-leave')['state'][field]=value
            with self.assertRaises(ValueError):v.semantic(changed,marks,1,True)

if __name__=='__main__':unittest.main()
