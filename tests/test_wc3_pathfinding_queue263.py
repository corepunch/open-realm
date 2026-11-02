"""Reject changed retail queue ownership, missing releases and untrusted captures."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_queue263 as v
class QueueTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def reject(self,edit):
  b=copy.deepcopy(self.bundle);edit(b)
  with self.assertRaises(ValueError):v.runtime(b,self.spec)
 def test_complete_original_contract(self):
  result=v.runtime(self.bundle,v.validate(self.spec))
  self.assertEqual(result,dict(captures=15,public_markers=543,native_scenarios=10,admissions=38,activations=32))
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop())
 def test_wrong_target_event(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='point-event'and r['order']['command']==v.ATTACK).update(event='target-event'))
 def test_missing_activation(self):
  self.reject(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows']if r.get('event')=='point-event'and r['order']['command']==v.ATTACK)))
 def test_append_changes_current_head(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='append-end'and r['state']['count']==2)['state'].update(user=[0,0]))
 def test_task_constructed_before_notification(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='point-event'and r['order']['command']==v.ATTACK)['state'].update(tasks=[0,0]))
 def test_rejected_repair_does_not_reenter(self):
  self.reject(lambda b:next(r for r in b['captures'][3]['rows']if r.get('event')=='dispatch'and r['depth']==2).update(depth=1))
 def test_canceled_successor_dispatched(self):
  self.reject(lambda b:next(r for r in b['captures'][6]['rows']if r.get('event')=='dispatch'and r['order']['command']==v.STOP)['order'].update(command=v.ATTACK))
 def test_live_target_loses_payload(self):
  self.reject(lambda b:next(r for r in b['captures'][12]['rows']if r.get('event')=='target-event')['order'].update(target=v.INVALID))
 def test_missing_release(self):
  self.reject(lambda b:b['captures'][0]['rows'].remove(next(r for r in b['captures'][0]['rows']if r.get('event')=='release')))
 def test_unbalanced_reference(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='release-end').update(references=9))
 def test_canonical_identity_survives_release(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='release-end').update(resolved=True))
 def test_final_queue_not_empty(self):
  self.reject(lambda b:[r for r in b['captures'][0]['rows']if r.get('event')=='state'][-1]['state'].update(count=1))
 def test_owner_environment(self):self.reject(lambda b:b['captures'][0]['rows'][0].update(env='A'))
 def test_failed_input(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='player-input').update(rc=1))
 def test_changed_control(self):self.reject(lambda b:b['captures'][2].update(preload=b['captures'][2]['preload'].replace('head=851983','head=0',1)))
 def test_missing_completion(self):self.reject(lambda b:b['captures'][0]['rows'].pop())
 def test_source_pin_changed(self):
  s=copy.deepcopy(self.spec);s['pins'][v.SOURCES[0]]='0'*64
  with self.assertRaises(ValueError):v.validate(s)
 def test_instruction_omission(self):
  s=copy.deepcopy(self.spec);s['instructions'].pop(next(iter(s['instructions'])))
  with self.assertRaises(ValueError):v.validate(s)
if __name__=='__main__':unittest.main()
