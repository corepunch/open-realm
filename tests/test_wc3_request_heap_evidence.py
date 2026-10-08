"""Request evidence rejects altered keys, cancellation, reuse and incomplete runs."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
import verify_order179_requests as requests

class RequestEvidenceTests(unittest.TestCase):
    def test_request_claims_require_keys_cancellation_reuse_repeat_and_control(self):
        frozen=json.loads((ROOT/requests.EXPECTED).read_bytes())
        self.assertEqual(requests.claims(frozen),[])
        for mutation in ('deadline','serial','order','cancel','reuse','repeat','control','pop'):
            changed=copy.deepcopy(frozen);live=changed['live']['requests'];rows=live['rows']
            if mutation in ('deadline','serial'):
                row=next(r for r in rows if r[0]=='Q' and r[1]==5)
                row[5 if mutation=='deadline' else 8]='3f000687' if mutation=='deadline' else 99
            elif mutation=='order':
                a,b=[i for i,r in enumerate(rows)if r[0]=='X' and r[1]==5][:2];rows[a],rows[b]=rows[b],rows[a]
            elif mutation=='cancel':next(r for r in rows if r[0]=='X' and r[7]==41)[6]='00000001'
            elif mutation=='reuse':next(r for r in rows if r[0]=='Q' and r[1]==10)[3]='B0'
            elif mutation=='repeat':live['repeat_equal']=False
            elif mutation=='control':live['observer_equals_control']['requests-observe-2.jsonl']=False
            else:live['ordercheck']['requests-observe-2.jsonl']['violations']=1
            with self.subTest(mutation=mutation):self.assertTrue(requests.claims(changed))

    def test_empty_launch_or_trace_end_without_complete_marker_is_rejected(self):
        with self.assertRaises(AssertionError):requests.complete_capture(b'',requests.digest(b''))
        rows=[dict(event='metadata',sha256=requests.BINARY_SHA,mode='observe'),
              dict(event='marker',value='RSO5 initialize'),dict(event='trace-end',installed=True,caps={}),
              dict(event='artifacts',errors=[])]
        def raw():return ('\n'.join(json.dumps(r)for r in rows)+'\n').encode()
        with self.assertRaises(AssertionError):requests.complete_capture(raw(),requests.digest(raw()))
        rows[1]['value']='RSO5 tick=45 phase=9 complete'
        self.assertEqual(requests.complete_capture(raw(),requests.digest(raw())),4)
        rows[2]['caps']={'queue':1}
        with self.assertRaises(AssertionError):requests.complete_capture(raw(),requests.digest(raw()))
        rows[2]['caps']={};rows[3]['errors']=['lost output']
        with self.assertRaises(AssertionError):requests.complete_capture(raw(),requests.digest(raw()))
if __name__=='__main__':unittest.main()
