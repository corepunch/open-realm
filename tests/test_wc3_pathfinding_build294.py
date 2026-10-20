"""Keep original queue/control expectations independent of engine changes."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_build294 as v
class Build294Evidence(unittest.TestCase):
 def setUp(self):self.spec=json.loads(v.SPEC.read_text())
 def reject(self,edit):
  s=copy.deepcopy(self.spec);edit(s)
  with self.assertRaises(ValueError):v.validate(s)
 def test_original_matrix(self):v.validate(self.spec)
 def test_source_substitution(self):self.reject(lambda s:s['source_sha256'].__setitem__(v.SOURCES[0],'0'*64))
 def test_missing_original_case(self):self.reject(lambda s:s['original'].pop())
 def test_busy_append_publishes(self):
  def edit(s):next(x for x in s['original']if x['busy'])['append']['trace'].append(dict(event='point-event'))
  self.reject(edit)
 def test_nested_move_dispatches_outer(self):
  def edit(s):next(x for x in s['original']if x['callback']=='nested')['activation'].append(dict(event='ability-execute'))
  self.reject(edit)
 def test_stop_retains_public_head(self):
  def edit(s):next(x for x in s['original']if x['callback']=='stop')['activation'][1]['head']=[1,101]
  self.reject(edit)
 def test_wrong_command(self):self.reject(lambda s:s['original'][0]['activation'][0].__setitem__('command',851986))
 def test_wrong_control_bits(self):self.reject(lambda s:s['original'][0].__setitem__('flags',0x84))
 def test_missing_instruction_guard(self):self.reject(lambda s:s['instructions'].pop(next(iter(s['instructions']))))
if __name__=='__main__':unittest.main()
