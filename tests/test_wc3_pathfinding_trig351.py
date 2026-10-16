import gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_trig351 as oracle
class TrigCallerEvidenceTests(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(oracle.SPEC.read_text());self.files=json.loads(gzip.decompress(oracle.BUNDLE.read_bytes()))
 def validate(self):return oracle.validate(self.spec,self.files)
 def edit(self,name,fn):
  value=json.loads(self.files[name]);fn(value);self.files[name]=json.dumps(value);self.spec['files'][name]=oracle.digest(self.files[name].encode())
 def rows(self,name,fn):
  value=[json.loads(l)for l in self.files[name].splitlines()];fn(value);self.files[name]='\n'.join(map(json.dumps,value))+'\n';self.spec['files'][name]=oracle.digest(self.files[name].encode())
 def test_complete_callers_and_public_observations(self):
  r=self.validate();self.assertEqual((r['sites'],r['functions'],r['instructions'],r['scalar_calls'],r['public_results']),(140,70,6897,454,84))
 def test_missing_call_site(self):
  self.edit('inventory.json',lambda x:x['calls']['6f06ffa0'].pop());
  with self.assertRaises(ValueError):self.validate()
 def test_missing_instruction(self):
  self.edit('inventory.json',lambda x:x['instructions'].popitem());
  with self.assertRaises(ValueError):self.validate()
 def test_mutated_static_output_definition(self):
  self.spec['ledger'][0]['output_definitions']=['external alias'];
  with self.assertRaises(ValueError):self.validate()
 def test_local_output_is_not_sufficient_when_it_aliases_a_local_input(self):
  def alias(x):
   row=x['functions']['6f1d4c80'];row['assembly']=row['assembly'].replace('6f1d4cb7 LEA ECX,[EBP + 0xc]','6f1d4cb7 LEA ECX,[EBP + -0x4]')
  inventory=json.loads(self.files['inventory.json']);alias(inventory)
  with self.assertRaisesRegex(ValueError,'concrete caller aliases trig input'):
   oracle.ledger(inventory)
 def test_wrong_capture_owner(self):
  self.rows('observe-a.jsonl',lambda rows:rows[0].update(owned=False))
  with self.assertRaises(ValueError):self.validate()
 def test_scalar_input_alias_cannot_be_relabelled_as_gameplay(self):
  def change(rows):
   row=next(r for r in rows if r['event']=='scalar-enter');row['equality']=[[True,True],[True,True]]
  self.rows('observe-a.jsonl',change)
  with self.assertRaises(ValueError):self.validate()
 def test_counter_difference(self):
  self.rows('observe-a.jsonl',lambda rows:next(r for r in rows if r['event']=='scalar-leave').update(c=999))
  with self.assertRaises(ValueError):self.validate()
 def test_incomplete_control(self):
  self.rows('control-a.jsonl',lambda rows:rows[-1].update(complete=False))
  with self.assertRaises(ValueError):self.validate()
 def test_changed_native_result(self):
  self.spec['public'][0]['result']^=1
  with self.assertRaises(ValueError):self.validate()
 def test_observer_free_markers_differ(self):
  name='control-a-preload.txt';self.files[name]=self.files[name].replace('label=complete','label=altered');self.spec['files'][name]=oracle.digest(self.files[name].encode())
  self.rows('control-a.jsonl',lambda rows:rows[-1].update(sha256=self.spec['files'][name]))
  with self.assertRaises(ValueError):self.validate()
if __name__=='__main__':unittest.main()
