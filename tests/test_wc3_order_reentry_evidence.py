"""Nested-order evidence rejects changed payloads, causality and incomplete probes."""
import copy,gzip,importlib.util,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
import verify_order178_reentry as nested
import verify_order178_removal as removal

class ReentryEvidenceTests(unittest.TestCase):
    def test_nested_claims_and_altered_delivery_payload_head_corpse_or_timing(self):
        frozen=json.loads(gzip.decompress((ROOT/nested.EXPECTED).read_bytes()))
        self.assertEqual(nested.claims(frozen),[])
        for old,new in [('enter trig=R2','enter trig=R9'),('px=-1700.000','px=0.000'),
                        (':851972 eval=',':0 eval='),(':0.000:0 eval=',':420.000:0 eval='),
                        ('op kill end','missing kill end')]:
            changed=copy.deepcopy(frozen);rows=changed['live']['controls']['forward']
            rows[:]=[s.replace(old,new)for s in rows]
            with self.subTest(old=old):self.assertTrue(nested.claims(changed))
        changed=copy.deepcopy(frozen);changed['live']['nested']['repeat_equal']=False
        self.assertTrue(nested.claims(changed))

    def test_fresh_admission_return_head_repeat_control_and_caller_stacks(self):
        frozen=json.loads((ROOT/removal.EXPECTED).read_bytes())
        captures=frozen['captures'];controls={c['scene']:c['markers']for c in frozen['controls']}
        self.assertEqual(removal.claims(captures,controls),[])
        for mutation in ('return','head','repeat','control','stack','off-count'):
            changed=copy.deepcopy(captures);control=copy.deepcopy(controls)
            if mutation=='return':
                for c in changed[:2]:c['markers']=[s.replace('result=true','result=false')for s in c['markers']]
            elif mutation=='head':
                for c in changed[:2]:c['markers']=[s.replace(':420.000:851986 eval=',':420.000:0 eval=')for s in c['markers']]
            elif mutation=='repeat':changed[1]['markers'].pop()
            elif mutation=='control':control['payload'].pop()
            elif mutation=='stack':changed[-1]['producers'][-1]['backtrace'][3]=0x48c992
            else:changed[-1]['producers'].pop()
            with self.subTest(mutation=mutation):self.assertTrue(removal.claims(changed,control))

    def test_trace_end_without_completed_probe_is_rejected(self):
        rows=[dict(event='metadata',sha256=removal.BINARY_SHA,mode='observe'),
              dict(event='marker',value='RSO3 initialize'),dict(event='trace-end',installed=True),dict(event='artifacts')]
        def raw():return ('\n'.join(json.dumps(r)for r in rows)+'\n').encode()
        with self.assertRaises(AssertionError):removal.read_capture(raw(),removal.digest(raw()))
        rows[1]['value']='RSO3 tick=35 phase=9 complete'
        self.assertEqual(len(removal.read_capture(raw(),removal.digest(raw()))[1]),1)
        rows.append(dict(event='script-error'))
        with self.assertRaises(AssertionError):removal.read_capture(raw(),removal.digest(raw()))
if __name__=='__main__':unittest.main()
