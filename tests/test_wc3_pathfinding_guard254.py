"""The guard producer may extend parity coverage, never rewrite retained retail rows."""
import copy
import gzip
import json
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_guard254 as v

class Guard254Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(v.FIXTURE.read_text())
        cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def test_original_repeats_control_and_unchanged_trajectory(self):
        result=v.verify_runtime(self.bundle,self.spec)
        self.assertEqual(result,dict(captures=2,controls=1,public_markers=645,raw_follower_rows=618,raw_target_rows=624,guard_arms=12))
        self.assertEqual(v.digest(ROOT/v.HEADER),self.spec['retained_header_sha256'])
        self.assertEqual(self.spec['engine_last_counter'],1723)

    def reject_event_change(self,event,field,value):
        b=copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows']if r.get('event')==event)[field]=value
        with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)

    def test_rejects_changed_guard_delay(self):
        self.reject_event_change('guard-arm254','delay',0)

    def test_rejects_changed_periodicity(self):
        self.reject_event_change('guard-arm254','periodic',0)

    def test_rejects_changed_guard_range(self):
        self.reject_event_change('guard-range254','range',0)

    def test_rejects_changed_retained_anchor(self):
        self.reject_event_change('guard-range254','anchor',[0,0])

    def test_rejects_fake_move_retry_producer(self):
        b=copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows']if r.get('event')=='point-start254'and r['c']==1482)['chain']=[]
        with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)

    def test_rejects_rewritten_restart_position(self):
        b=copy.deepcopy(self.bundle)
        next(r for r in b['captures'][0]['rows']if r.get('event')=='point-stop254'and r['c']==1482)['after'][0]+=1
        with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)

    def test_rejects_missing_completion(self):
        b=copy.deepcopy(self.bundle);b['captures'][0]['rows'][-1]['complete']=False
        with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)

    def test_rejects_instrumented_control(self):
        b=copy.deepcopy(self.bundle);b['controls'][0]['rows'].insert(1,dict(event='guard-arm254'))
        with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)

    def test_rejects_changed_old_fixture_pin(self):
        s=copy.deepcopy(self.spec);s['pins'][v.HEADER]='0'*64
        with self.assertRaises(ValueError):v.validate(s)

    def test_rejects_shorter_engine_scope(self):
        s=copy.deepcopy(self.spec);s['engine_last_counter']=1474
        with self.assertRaises(ValueError):v.validate(s)

if __name__=='__main__':unittest.main()
