"""Keep Attack TargetLost evidence distinct from fog arrival and Move fallback."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_attack_recovery223 as V
class AttackRecoveryContractTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def row(self,b,event,scene=0,caller=None):
  return next(r for r in b['captures'][0]['rows']if r.get('scene')==scene and r['event']==event and (caller is None or r.get('caller')==caller))
 def reject(self,fn):
  b=copy.deepcopy(self.bundle);fn(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def test_repeated_original_and_unhooked_control(self):
  self.assertEqual(V.validate_runtime(self.bundle,V.validate(self.spec)),dict(captures=2,controls=1,scenes=5,public_markers=2997))
 def test_source_and_bundle_pins(self):
  for p in V.SOURCES:
   s=copy.deepcopy(self.spec);s['pins'][p]='0'*64
   with self.assertRaises(ValueError):V.validate(s)
  s=copy.deepcopy(self.spec);s['bundle_sha256']='0'*64
  with self.assertRaises(ValueError):V.validate(s)
 def test_loss_uses_detection_only(self):
  self.reject(lambda b:self.row(b,'validate',caller='49b5b7').update(visibility=1))
 def test_undetected_invisibility_fails(self):
  self.reject(lambda b:self.row(b,'validate',caller='49b5b7').update(result=0))
 def test_truesight_retains_target(self):
  self.reject(lambda b:self.row(b,'validate',1,'49b5b7').update(result=0xdd))
 def test_recovery_uses_internal_point_task(self):
  self.reject(lambda b:self.row(b,'point-task',caller='49d490').update(code=0xd016b))
 def test_recovery_uses_captured_pose_and_range(self):
  for key,val in [('point',[0,0]),('range',0)]:
   self.reject(lambda b:self.row(b,'point-task',2,'49d490').update({key:val}))
 def test_recovery_retains_public_attack_head(self):
  self.reject(lambda b:self.row(b,'point-task',caller='49d490')['unit'].update(head=V.INVALID))
 def test_loss_flag_and_original_ordering(self):
  self.reject(lambda b:self.row(b,'recover',caller='49b594').update(flags=4))
  self.reject(lambda b:self.row(b,'point-task',caller='49d490').update(seq=0))
 def test_release_must_precede_point_task(self):
  def change(b):
   p=self.row(b,'point-task',caller='49d490');start=self.row(b,'recover',caller='49b594')['seq'];r=next(r for r in b['captures'][0]['rows']if r['event']=='release-target' and r.get('scene')==0 and r['unit']['identity']==p['unit']['identity'] and start<r['seq']<p['seq'])
   r['seq']=p['seq']+1
  self.reject(change)
 def test_control_and_every_marker_must_be_complete(self):
  self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))
  self.reject(lambda b:b['captures'].pop())
  for i in range(3):
   self.reject(lambda b:b['captures'][i]['rows'][-1].update(complete=False))
   self.reject(lambda b:b['captures'][i].__setitem__('preload',b['captures'][i]['preload'].replace('shared=0','shared=1')))
 def test_runtime_module_base_is_not_frozen(self):
  b=copy.deepcopy(self.bundle);next(r for r in b['captures'][0]['rows']if r['event']=='module')['base']='0x10000000'
  self.assertEqual(V.validate_runtime(b,self.spec)['scenes'],5)
if __name__=='__main__':unittest.main()
