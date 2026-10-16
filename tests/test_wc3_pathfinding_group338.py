import copy,gzip,importlib.util,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_group338 as verify

class Group338EvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(verify.SPEC.read_text())
        cls.cases=json.loads(gzip.decompress(verify.BUNDLE.read_bytes()))
        cls.town_cases=json.loads(gzip.decompress(verify.TOWN_BUNDLE.read_bytes()))
        cls.states_cases=json.loads(gzip.decompress(verify.STATES_BUNDLE.read_bytes()))

    def test_complete_control_effect_repeats_and_control(self):
        result=verify.states_runtime(self.states_cases,self.spec['states'])
        self.assertEqual(result['control_effect_native_events'],476)

    def test_changed_roots_override_rejected(self):
        cases=copy.deepcopy(self.states_cases)
        row=next(r for r in cases[0]['rows'] if r.get('scene')==2 and
                 r.get('event')=='predicate' and r.get('address')=='48e260')
        row['result']=0
        with self.assertRaises(ValueError):verify.states_runtime(cases,self.spec['states'])

    def test_missing_control_effect_repeat_rejected(self):
        with self.assertRaises(ValueError):verify.states_runtime(self.states_cases[:2],self.spec['states'])

    def test_complete_town_repeats_and_control(self):
        result=verify.town_runtime(self.town_cases,self.spec['town'])
        self.assertEqual(result['town_native_events'],3916)
        self.assertEqual(result['town_candidate_cases'],38)

    def test_town_changed_priority_rejected(self):
        cases=copy.deepcopy(self.town_cases)
        row=next(r for r in cases[0]['rows'] if r.get('event')=='rank-end')
        row['key'][0]^=0x10000
        with self.assertRaises(ValueError):verify.town_runtime(cases,self.spec['town'])

    def test_town_missing_repeat_rejected(self):
        with self.assertRaises(ValueError):verify.town_runtime(self.town_cases[:2],self.spec['town'])

    def test_town_unfinished_observer_rejected(self):
        cases=copy.deepcopy(self.town_cases)
        next(r for r in cases[0]['rows'] if r['event']=='trace-end')['depth']=1
        with self.assertRaises(ValueError):verify.town_runtime(cases,self.spec['town'])

    def test_town_instrumented_control_rejected(self):
        cases=copy.deepcopy(self.town_cases)
        cases[-1]['rows'].insert(1,{'event':'marker','seq':1})
        with self.assertRaises(ValueError):verify.town_runtime(cases,self.spec['town'])

    def test_complete_repeats_and_control(self):
        result=verify.runtime(self.cases,self.spec)
        self.assertEqual(result['native_events'],3198)
        self.assertEqual(result['artillery_domains'],6)

    def test_changed_priority_rejected(self):
        cases=copy.deepcopy(self.cases)
        row=next(r for r in cases[0]['rows'] if r.get('event')=='rank-end' and r.get('scene')==81)
        row['key']=[0x64000000,0x0be00000]
        with self.assertRaises(ValueError):verify.runtime(cases,self.spec)

    def test_instrumented_control_rejected(self):
        cases=copy.deepcopy(self.cases);cases[-1]['rows'].insert(1,{'event':'marker','seq':1})
        with self.assertRaises(ValueError):verify.runtime(cases,self.spec)

    def test_missing_repeat_rejected(self):
        with self.assertRaises(ValueError):verify.runtime(self.cases[:2],self.spec)

    def test_public_motion_change_rejected(self):
        cases=copy.deepcopy(self.cases)
        cases[-1]['preload']=cases[-1]['preload'].replace('x=512.0000','x=513.0000',1)
        with self.assertRaises(ValueError):verify.runtime(cases,self.spec)

    def test_unfinished_observer_rejected(self):
        cases=copy.deepcopy(self.cases)
        next(r for r in cases[0]['rows'] if r['event']=='trace-end')['depth']=1
        with self.assertRaises(ValueError):verify.runtime(cases,self.spec)

if __name__=='__main__':unittest.main()
