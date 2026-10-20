"""Retained request identities and frozen trajectory words are independent gates."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_guard255 as v
class Guard255Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def test_original_rearm_repeats_and_control(self):
  self.assertEqual(v.verify_runtime(self.bundle,self.spec)['guard_rearms'],8)
 def test_changed_serial_fails_even_with_rewritten_trace_expectation(self):
  b=copy.deepcopy(self.bundle);s=copy.deepcopy(self.spec)
  next(r for r in b['captures'][0]['rows']if r.get('event')=='guard-rearm255')['after']['serial']+=1
  s['requests']=v.normalized(b['captures'][0]['rows'])
  with self.assertRaises(ValueError):v.verify_runtime(b,s)
 def test_request_reallocation_is_rejected(self):
  b=copy.deepcopy(self.bundle)
  next(r for r in b['captures'][0]['rows']if r.get('event')=='guard-rearm255')['after']['request']='0x1'
  with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)
 def test_canceled_request_cannot_rearm(self):
  b=copy.deepcopy(self.bundle)
  next(r for r in b['captures'][0]['rows']if r.get('event')=='guard-execute255'and r['before']['flags']&0x10000)['after']['deadline']+=1
  with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)
 def test_agent_dispatcher_with_no_guard_events_is_not_evidence(self):
  b=copy.deepcopy(self.bundle);b['captures'][0]['rows']=[r for r in b['captures'][0]['rows']if not r.get('event','').startswith('guard-')]
  with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)
 def test_incomplete_capture_is_rejected(self):
  b=copy.deepcopy(self.bundle);b['captures'][0]['rows'][-1]['complete']=False
  with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)
 def test_control_must_remain_uninstrumented(self):
  b=copy.deepcopy(self.bundle);b['controls'][0]['rows'].insert(1,dict(event='guard-rearm255'))
  with self.assertRaises(ValueError):v.verify_runtime(b,self.spec)
if __name__=='__main__':unittest.main()
