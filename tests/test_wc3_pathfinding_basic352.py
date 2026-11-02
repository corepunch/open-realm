"""Evidence rejection tests for caller storage and the real formatting-to-Move bug."""
import gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_basic352 as oracle
class BasicCallerEvidenceTests(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(oracle.SPEC.read_text());self.files=json.loads(gzip.decompress(oracle.BUNDLE.read_bytes()))
 def validate(self):return oracle.validate(self.spec,self.files)
 def edit(self,name,fn):
  value=json.loads(self.files[name]);fn(value);self.files[name]=json.dumps(value);self.spec['files'][name]=oracle.digest(self.files[name].encode())
 def rows(self,name,fn):
  value=[json.loads(l)for l in self.files[name].splitlines()];fn(value);self.files[name]='\n'.join(map(json.dumps,value))+'\n';self.spec['files'][name]=oracle.digest(self.files[name].encode())
 def test_complete_callers_public_conversion_and_declared_boundary(self):
  r=self.validate();self.assertEqual((r['references'],r['storage_sites'],r['instructions'],r['markers']),(7097,6912,179722,507));self.assertIn('no frame/trajectory parity',r['diagnostic_broad_queries'])
 def test_missing_call_site(self):
  self.edit('inventory.json',lambda x:x['calls']['6f06f9c0'].pop())
  with self.assertRaises(ValueError):self.validate()
 def test_missing_instruction(self):
  self.edit('inventory.json',lambda x:x['instructions'].popitem())
  with self.assertRaises(ValueError):self.validate()
 def test_uncontained_caller(self):
  self.edit('inventory.json',lambda x:x['calls']['6f06f9c0'][0].update(entry=''))
  with self.assertRaises(ValueError):self.validate()
 def test_storage_proof_cannot_be_replaced_by_reference_counts(self):
  self.spec['storage_sha256']='0'*64
  with self.assertRaisesRegex(ValueError,'storage'):self.validate()
 def test_missing_startup_thunk(self):
  self.edit('startup.json',lambda x:x.pop())
  with self.assertRaisesRegex(ValueError,'startup'):self.validate()
 def test_wrong_capture_owner(self):
  self.rows('format/observe-a.jsonl',lambda rows:rows[0].update(owned=False))
  with self.assertRaises(ValueError):self.validate()
 def test_scalar_pointer_abi(self):
  self.rows('format/observe-a.jsonl',lambda rows:next(r for r in rows if r['event']=='scalar-leave'and r['outputs']).update(result=0))
  with self.assertRaises(ValueError):self.validate()
 def test_scalar_counter_difference(self):
  self.rows('format/observe-a.jsonl',lambda rows:next(r for r in rows if r['event']=='scalar-leave').update(c=999))
  with self.assertRaises(ValueError):self.validate()
 def test_incomplete_control(self):
  self.rows('basic/control-a.jsonl',lambda rows:rows[-1].update(complete=False))
  with self.assertRaises(ValueError):self.validate()
 def test_changed_literal_native_result(self):
  self.spec['public'][0]['value']='invented'
  with self.assertRaises(ValueError):self.validate()
 def test_original_move_input_cannot_use_host_formatting(self):
  self.rows('format/observe-a.jsonl',lambda rows:next(r for r in rows if r['event']=='point-order').update(x=0x44900400))
  with self.assertRaises(ValueError):self.validate()
 def test_partial_overlap_controls_remain_distinct_from_public_inputs(self):
  self.assertEqual(self.spec['aliases']['partial_overlap_cases'],16470)
  rows=[json.loads(l)for l in self.files['format/observe-a.jsonl'].splitlines()]
  scalar=next(r for r in rows if r['event']=='scalar-enter')
  self.assertEqual(len(set(scalar['pointers'])),len(scalar['pointers']))
if __name__=='__main__':unittest.main()
