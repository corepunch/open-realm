"""Request evidence rejects altered keys, cancellation, reuse and incomplete runs."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra/research'))
import verify_order179_requests as requests
import verify_order180_ranges as ranges
import verify_order181_clocks as clocks

class RequestEvidenceTests(unittest.TestCase):
    def test_clock_claims_require_exact_rebase_restore_and_continuation(self):
        frozen=json.loads((ROOT/clocks.EXPECTED).read_bytes())
        self.assertEqual(clocks.claims(frozen),[])
        for mutation in('epoch','rebase','remainder','presentation','ties','saved','poll','release','clock','markers','rows','control','pop'):
            changed=copy.deepcopy(frozen);wrap=changed['live']['wrap'];load=changed['live']['saveload']
            if mutation=='epoch':wrap['rebase'][3][3]=2
            elif mutation=='rebase':wrap['rebase'][3][5][0][0]='3e000000'
            elif mutation=='remainder':wrap['near_span_primary'][-1][1]='3ba10001'
            elif mutation=='presentation':wrap['near_span_presentation'][-1][2]=0
            elif mutation=='ties':
                a,b=[i for i,r in enumerate(wrap['around_primary_wrap'])if r[0]=='execute'and r[1]=='4395ffff']
                rows=wrap['around_primary_wrap'];rows[a],rows[b]=rows[b],rows[a]
            elif mutation=='saved':load['request_save'][0][1]='41f10000'
            elif mutation=='poll':next(r for r in load['wrapper_load']if r[0]=='a91220')[1][2]=39
            elif mutation=='release':next(r for r in load['wrapper_load']if r[0]=='a8099c')[2][0]='41f05e54'
            elif mutation=='clock':load['load_clock'][1][1]['serial']=133
            elif mutation=='markers':load['pre_continuation_vs_post_load_markers']['equal']=False
            elif mutation=='rows':load['pre_continuation_vs_post_load_window_rows']['equal']=False
            elif mutation=='control':load['control']['equal']=False
            else:wrap['ordercheck']['violations']=1
            with self.subTest(mutation=mutation):self.assertTrue(clocks.claims(changed))

    def test_clock_teardown_requires_old_cancelled_pops_and_load_start_flush(self):
        rows=[dict(event='settle-enter',seq=10,tick=640,clocks={'primary':{'timeW':'4281f482'}}),
              dict(event='execute',seq=12,req={'deadlineW':'4281ffff','serial':38,'flags':'00010001'}),
              dict(event='execute',seq=13,req={'deadlineW':'4281ffff','serial':41,'flags':'00010001'}),
              dict(event='settle-leave',seq=15,clocks={'primary':{'timeW':'00000000'}}),
              dict(event='game-load-enter',seq=20),dict(event='settle-enter',seq=21,tick=640,by='4cefc',clocks={})]
        self.assertEqual(clocks.teardown_claims(rows),[])
        for index,key,value in((1,'serial',99),(2,'deadlineW','4282ffff'),(2,'flags','00000001')):
            changed=copy.deepcopy(rows);changed[index]['req'][key]=value
            self.assertTrue(clocks.teardown_claims(changed))
        for index in(0,1,2,3,4,5):
            changed=copy.deepcopy(rows);changed.pop(index)
            self.assertTrue(clocks.teardown_claims(changed))

    def test_range_claims_require_registration_rearm_release_and_repeat_ties(self):
        frozen=json.loads((ROOT/ranges.EXPECTED).read_bytes())
        self.assertEqual(ranges.claims(frozen),[])
        for mutation in ('peer','start','peer-release','self-release','rearm','cancel','ties','repeat','control','pop'):
            changed=copy.deepcopy(frozen);live=changed['live']['requests'];rows=live['rows']
            if mutation=='peer':next(r for r in rows if r[0]=='X'and r[7]==45 and r[4]=='4037fffc')[6]='00010001'
            elif mutation=='start':next(r for r in rows if r[0]=='Q'and r[8]==77)[-1]='40380000'
            elif mutation in('peer-release','self-release'):
                serial=80 if mutation=='peer-release'else 99
                next(r for r in rows if r[0]=='Q'and r[8]==serial)[5]='00000000'
            elif mutation=='rearm':rows.remove(next(r for r in rows if r[0]=='R'and r[7]==48 and r[4]=='406e6662'))
            elif mutation=='cancel':next(r for r in rows if r[0]=='X'and r[7]==45 and r[4]=='403ffffc')[6]='00000001'
            elif mutation=='ties':
                a,b=[i for i,r in enumerate(rows)if r[0]=='X'and r[4]=='4067fffc'and r[6]=='00000001'];rows[a],rows[b]=rows[b],rows[a]
            elif mutation=='repeat':live['repeat_equal']=False
            elif mutation=='control':live['observer_equals_control']['requests-observe-2.jsonl']=False
            else:live['ordercheck']['requests-observe-2.jsonl']['violations']=1
            with self.subTest(mutation=mutation):self.assertTrue(ranges.claims(changed))

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
