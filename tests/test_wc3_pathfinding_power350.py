import gzip,json
from pathlib import Path
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_power350 as oracle
class PublicPowerLifetimeEvidenceTests(unittest.TestCase):
 def setUp(self):
  self.spec=json.loads(oracle.SPEC.read_text());self.files=json.loads(gzip.decompress(oracle.BUNDLE.read_bytes()))
 def change(self,names,predicate,edit):
  for name in names:
   rows=[json.loads(l)for l in self.files[name].splitlines()]
   for row in rows:
    if predicate(row):edit(row)
   self.files[name]='\n'.join(json.dumps(r)for r in rows)+'\n';self.spec['files'][name]=oracle.digest(self.files[name].encode())
 def reject(self):
  with self.assertRaises(ValueError):oracle.validate(self.spec,self.files)
 def test_complete_bounded_repeats_and_observer_free_controls(self):
  r=oracle.validate(self.spec,self.files);self.assertEqual((r['captures'],r['controls'],r['samples']),(8,4,295));self.assertEqual([len(r['records'][i])for i in range(4)],[15,15,15,23])
 def test_unowned_control(self):
  self.change(['control-0-a.jsonl'],lambda r:r['event']=='metadata',lambda r:r.update(owned=False));self.reject()
 def test_short_observer_free_hold(self):
  self.change(['control-0-a.jsonl'],lambda r:r['event']=='bounded-control-deadline',lambda r:r.update(hold=1));self.reject()
 def test_primary_counter_progress_is_not_native_nonreturn(self):
  self.change(['observe-0-a.jsonl','observe-0-b.jsonl'],lambda r:r['event']=='lifetime-sample',lambda r:r.update(c=1091));self.reject()
 def test_thread_outside_verified_math_loop(self):
  self.change(['observe-1-a.jsonl','observe-1-b.jsonl'],lambda r:r['event']=='lifetime-sample',lambda r:r.update(pc='0x919123'));self.reject()
 def test_exp_sign_and_operation_order(self):
  self.change(['observe-1-a.jsonl','observe-1-b.jsonl'],lambda r:r['event']=='reduction-enter'and r['name']=='exp',lambda r:r.update(input=0x4e254258));self.reject()
 def test_exp_integer_conversion(self):
  self.change(['observe-2-a.jsonl','observe-2-b.jsonl'],lambda r:r['event']=='integer-power-enter',lambda r:r.update(exponent=0));self.reject()
 def test_bytecode_instruction_guard_is_required(self):
  self.spec['instructions'].popitem();self.reject()
 def test_map_provenance(self):
  self.spec['maps']['0']='0'*64;self.reject()
 def test_archive_pin(self):
  self.files['control-0-a-before.txt']+=' ';self.reject()
if __name__=='__main__':unittest.main()
