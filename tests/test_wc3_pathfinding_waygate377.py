#!/usr/bin/env python3
"""Keep the complete Waygate transactions, controls and pointer provenance strict."""
import copy,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_waygate377 as v
class WaygatePointTasks(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(v.SPEC.read_text());cls.bundle,cls.result=v.validate(cls.spec)
 def reject(self,change):
  bundle=copy.deepcopy(self.bundle);change(bundle)
  with self.assertRaises((ValueError,AssertionError,KeyError,StopIteration)):v.validate_runtime(bundle)
 def edit(self,bundle,event,change,family=0,observation=0):
  cap=bundle['families'][family]['captures'][observation];rows=[json.loads(x)for x in cap['raw_jsonl'].splitlines()]
  change(next(r for r in rows if r['event']==event));cap['raw_jsonl']='\n'.join(json.dumps(x)for x in rows)+'\n'
 def test_complete_repeat_and_control_families(self):self.assertEqual(self.result,dict(observations=6,controls=3,public_markers=1333,semantic_records=9838,cohort_semantic_records=3139,cohort_interval_public_markers=243,cohort_public_control_markers=288))
 def test_request_command_is_literal(self):self.reject(lambda b:self.edit(b,'move-producer-enter',lambda r:r.update(code=r['code']^1)))
 def test_complete_arrival_record_is_retained(self):self.reject(lambda b:self.edit(b,'move-arrival',lambda r:r.update(unexpected_arrival_word=1)))
 def test_unknown_hexadecimal_fields_remain_literal(self):
  rows=[json.loads(x)for x in self.bundle['families'][0]['captures'][0]['raw_jsonl'].splitlines()]
  a=copy.deepcopy(rows);b=copy.deepcopy(rows)
  next(r for r in a if r['event']=='move-producer-enter')['new_hex_literal']='0x12345678'
  next(r for r in b if r['event']=='move-producer-enter')['new_hex_literal']='0x87654321'
  self.assertNotEqual(v.project(a,[0,1,2,3]),v.project(b,[0,1,2,3]))
 def test_bridge_offset_requires_unit_identity(self):self.reject(lambda b:self.edit(b,'point-request',lambda r:r.update(bridge=hex(int(r['bridge'],16)+4))))
 def test_control_suffix_cannot_be_dropped(self):self.reject(lambda b:b['families'][1]['captures'][2].update(preload=b['families'][1]['captures'][2]['preload'].replace('T377 tick=','corrupted tick=',1)))
 def test_control_capture_is_required(self):self.reject(lambda b:b['families'][0]['captures'].pop())
 def test_late_smart_scene_is_required(self):
  def change(b):
   cap=b['families'][1]['captures'][0];rows=[json.loads(x)for x in cap['raw_jsonl'].splitlines()];cap['raw_jsonl']='\n'.join(json.dumps(r)for r in rows if r.get('scene')!=4)+'\n'
  self.reject(change)
 def test_guard_inventory_is_complete(self):
  spec=copy.deepcopy(self.spec);spec['instructions'].pop(next(iter(spec['instructions'])))
  with self.assertRaises(ValueError):v.validate(spec)
 def test_source_pins_are_enforced(self):
  spec=copy.deepcopy(self.spec);spec['pins'][next(iter(spec['pins']))]='0'*64
  with self.assertRaises(ValueError):v.validate(spec)
 def cohort_edit(self,bundle,event,change):
  cap=bundle['cohort']['captures'][0];rows=[json.loads(x)for x in cap['raw_jsonl'].splitlines()]
  change(next(r for r in rows if r['event']==event and r.get('tick',-1)>40));cap['raw_jsonl']='\n'.join(json.dumps(x)for x in rows)+'\n'
 def test_cohort_task_identity_is_literal(self):self.reject(lambda b:self.cohort_edit(b,'point-task-consume',lambda r:r['unit']['task'].__setitem__(1,r['unit']['task'][1]+1)))
 def test_cohort_fine_word_is_literal(self):self.reject(lambda b:self.cohort_edit(b,'arrival',lambda r:r.update(heading=r['heading']^1)))
 def test_cohort_unknown_hex_is_literal(self):self.reject(lambda b:self.cohort_edit(b,'point-task',lambda r:r.update(association='0x11a20098')))
 def test_cohort_unknown_field_is_literal(self):self.reject(lambda b:self.cohort_edit(b,'arrival',lambda r:r.update(unexpected='0x11a20098')))
 def test_cohort_unbound_mover_rejected(self):self.reject(lambda b:self.cohort_edit(b,'arrival',lambda r:r['before'].update(mover='0x12345678')))
 def test_cohort_complete_control_required(self):self.reject(lambda b:b['cohort']['captures'][2].update(preload=b['cohort']['captures'][2]['preload'].replace(' label=complete',' label=changed')))
 def test_cohort_literal_record_order(self):
  def change(b):
   cap=b['cohort']['captures'][0];rows=[json.loads(x)for x in cap['raw_jsonl'].splitlines()];i=next(i for i,r in enumerate(rows)if r['event']=='arrival'and r.get('tick',-1)>40);rows[i],rows[i+1]=rows[i+1],rows[i];cap['raw_jsonl']='\n'.join(json.dumps(x)for x in rows)+'\n'
  self.reject(change)
if __name__=='__main__':unittest.main()
