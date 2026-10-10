"""Frozen retail work eligibility cannot be replaced with engine expectations."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work226 as V
class WorkContractTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def row(self,b,event):return next(r for r in b['captures'][0]['rows']if r['event']==event)
 def reject(self,fn):
  b=copy.deepcopy(self.bundle);fn(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def test_complete_repeat_and_unhooked_control(self):
  self.assertEqual(V.validate_runtime(self.bundle,V.validate(self.spec)),
   dict(captures=2,controls=1,public_markers=654,native_events=396,work_phases=24))
 def test_every_source_and_bundle_pin(self):
  for p in V.SOURCES:
   s=copy.deepcopy(self.spec);s['pins'][p]='0'*64
   with self.assertRaises(ValueError):V.validate(s)
  s=copy.deepcopy(self.spec);s['bundle_sha256']='0'*64
  with self.assertRaises(ValueError):V.validate(s)
 def test_instruction_bodies_required(self):
  s=copy.deepcopy(self.spec);s['instructions'].pop(next(iter(s['instructions'])))
  with self.assertRaises(ValueError):V.validate(s)
 def test_work_and_counted_suppression_are_independent(self):
  for event in ('begin-work-enter','end-work-leave','acquire-leave','release-enter'):
   self.reject(lambda b:self.row(b,event)[('after'if event.endswith('leave')else'before')].update(disableDepth=99))
  self.reject(lambda b:self.row(b,'begin-work-leave')['after'].update(work=0))
 def test_configuration_order_and_authored_fields_preserved(self):
  self.reject(lambda b:self.row(b,'configuration').update(seq=0))
  for i in range(4):self.reject(lambda b:self.row(b,'configuration')['args'].__setitem__(i,0xffffffff))
 def test_unknown_return_trampoline_not_a_native_callsite(self):
  self.assertTrue(any(r.get('caller')=='interceptor-tail-transfer'for r in self.spec['events']))
  self.reject(lambda b:self.row(b,'refresh-enter').update(caller='fake-native-callsite'))
 def test_missing_control_or_observer_completion_rejected(self):
  self.reject(lambda b:b['captures'].pop())
  for n in range(3):self.reject(lambda b:b['captures'][n]['rows'][-1].update(complete=False))
  self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))
 def test_public_positions_and_phase_words_are_frozen(self):
  self.reject(lambda b:b['captures'][2].__setitem__('preload',b['captures'][2]['preload'].replace('x=640.000','x=0.000')))
  self.reject(lambda b:self.row(b,'configuration')['state'].update(rawcode=0))
 def test_process_identities_are_not_expectations(self):
  b=copy.deepcopy(self.bundle)
  for c in b['captures']:
   for r in c['rows']:
    for k in ('before','after','state'):
     if k in r:r[k]['identity']=[1,2];r[k]['moverIdentity']=[3,4]
  self.assertEqual(V.validate_runtime(b,self.spec)['work_phases'],24)
 def test_engine_checked_against_native_phase_policy(self):
  log='\n'.join(f"W226 engine profile={s['profile']} stage={s['stage']} enable={s['enable']} policy={s['policy']:08x}"for s in self.spec['phases'])
  self.assertEqual(V.validate_engine(log,self.spec),24)
  for bad in (log.replace('stage=work enable=0','stage=work enable=1'),log.split('\n',1)[1]):
   with self.assertRaises(ValueError):V.validate_engine(bad,self.spec)
if __name__=='__main__':unittest.main()
