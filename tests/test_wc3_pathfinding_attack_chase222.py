"""Attack chase oracles retain native visibility gates and observer-free controls."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_attack_chase222 as V
class AttackChaseContractTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,fn):
  b=copy.deepcopy(self.bundle);fn(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def row(self,b,event,scene=0):return next(r for r in b['captures'][0]['rows']if r.get('scene')==scene and r['event']==event)
 def test_repeated_original_and_unhooked_control(self):
  self.assertEqual(V.validate_runtime(self.bundle,V.validate(self.spec)),dict(captures=2,controls=1,scenes=5,public_markers=3000))
 def test_source_and_bundle_pins(self):
  for p in V.SOURCES:
   s=copy.deepcopy(self.spec);s['pins'][p]='0'*64
   with self.assertRaises(ValueError):V.validate(s)
  s=copy.deepcopy(self.spec);s['bundle_sha256']='0'*64
  with self.assertRaises(ValueError):V.validate(s)
 def test_arrival_requires_full_visibility(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r['event']=='validate' and r.get('scene')==0 and r['result']==0xdd).update(visibility=0))
 def test_fogged_arrival_must_fail(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r['event']=='validate' and r.get('scene')==0 and r['result']==0xdd).update(result=0))
 def test_hidden_cached_destination_and_countdown(self):
  for field,value in [('dest',[0,0]),('cd',999),('unseen',0)]:
   self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r['event']=='group' and r.get('scene')==0 and r['unseen']>1).update({field:value}))
 def test_physical_target_request_is_not_persistent_follow(self):
  self.reject(lambda b:self.row(b,'target-request').update(persistent=1))
  self.reject(lambda b:self.row(b,'target-request').update(range=0))
 def test_short_fog_reacquires(self):
  self.reject(lambda b:self.row(b,'validate',1).update(result=0xdd))
 def test_native_recovery_identity_is_preserved(self):
  self.reject(lambda b:self.row(b,'recover').update(caller='0'))
 def test_public_control_is_not_instrumented(self):
  self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))
  self.reject(lambda b:b['captures'].pop())
 def test_every_capture_and_marker_must_be_complete(self):
  for i in range(3):
   self.reject(lambda b:b['captures'][i]['rows'][-1].update(complete=False))
   self.reject(lambda b:b['captures'][i].__setitem__('preload',b['captures'][i]['preload'].replace('label=restored','label=wrong')))
 def test_runtime_module_addresses_are_not_fixed_expectations(self):
  b=copy.deepcopy(self.bundle);next(r for r in b['captures'][0]['rows']if r['event']=='module')['base']='0x10000000'
  self.assertEqual(V.validate_runtime(b,self.spec)['scenes'],5)
if __name__=='__main__':unittest.main()
