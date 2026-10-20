"""Preserve original Blink-specific Attack range and point-recovery rules."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_attack_blink224 as V
class AttackBlinkContractTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def row(self,b,event,scene=0,caller=None):
  return next(r for r in b['captures'][0]['rows']if r.get('scene')==scene and r['event']==event and (caller is None or r.get('caller')==caller))
 def reject(self,fn):
  b=copy.deepcopy(self.bundle);fn(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def test_original_repeats_and_control(self):
  self.assertEqual(V.validate_runtime(self.bundle,V.validate(self.spec)),dict(captures=2,controls=1,scenes=4,public_markers=2385))
 def test_all_source_and_bundle_pins(self):
  for p in V.SOURCES:
   s=copy.deepcopy(self.spec);s['pins'][p]='0'*64
   with self.assertRaises(ValueError):V.validate(s)
  s=copy.deepcopy(self.spec);s['bundle_sha256']='0'*64
  with self.assertRaises(ValueError):V.validate(s)
 def test_original_instruction_and_scene_inventory_is_complete(self):
  for key in ('instructions','cases'):
   s=copy.deepcopy(self.spec)
   if key=='instructions':s[key].pop(next(iter(s[key])))
   else:s[key].pop()
   with self.assertRaises(ValueError):V.validate(s)
 def test_committed_positions_are_required(self):
  self.reject(lambda b:self.row(b,'range-check',caller='49b55a').update(predict=1))
 def test_original_range_constant_is_required(self):
  for k in ('range','constant'):
   self.reject(lambda b:self.row(b,'range-check',caller='49b55a').update({k:0}))
 def test_near_blink_retains_attack(self):
  self.reject(lambda b:self.row(b,'range-check',caller='49b55a').update(result=0))
  self.reject(lambda b:self.row(b,'validate',caller='49b5b7').update(result=0xdd))
 def test_far_blink_bypasses_validation(self):
  self.reject(lambda b:self.row(b,'range-check',1,'49b55a').update(result=1))
  self.reject(lambda b:self.row(b,'recover',1,'49b594').update(flags=4))
 def test_far_blink_excludes_point_recovery(self):
  def change(b):
   row=copy.deepcopy(self.row(b,'recover',1,'49b594'))
   row.update(event='point-task',caller='49d490');b['captures'][0]['rows'].insert(-2,row)
   next(r for r in b['captures'][0]['rows']if r['event']=='trace-end')['counts']['point-task']+=1
  self.reject(change)
 def test_far_blink_releases_after_recovery(self):
  self.reject(lambda b:self.row(b,'release-target',1,'49d3ff').update(seq=0))
 def test_near_blink_retains_public_head(self):
  self.reject(lambda b:self.row(b,'validate',caller='49b5b7')['unit'].update(head=V.INVALID))
 def test_every_marker_and_control_required(self):
  self.reject(lambda b:b['captures'].pop())
  self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))
  for i in range(3):
   self.reject(lambda b:b['captures'][i]['rows'][-1].update(complete=False))
   self.reject(lambda b:b['captures'][i].__setitem__('preload',b['captures'][i]['preload'].replace('shared=0','shared=1')))
 def test_runtime_addresses_are_not_frozen(self):
  b=copy.deepcopy(self.bundle);next(r for r in b['captures'][0]['rows']if r['event']=='module')['base']='0x10000000'
  self.assertEqual(V.validate_runtime(b,self.spec)['scenes'],4)
if __name__=='__main__':unittest.main()
