import copy,gzip,importlib.util,json,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('captain378',ROOT/'tools/ghidra/verify_wc3_pathing_captain378_roles.py')
import sys
sys.path.insert(0,str(ROOT/'tools/ghidra'))
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
class Captain378Evidence(unittest.TestCase):
 def setUp(self):self.data=json.loads(gzip.decompress(v.SPEC.read_bytes()))
 def test_complete_literal_sequence(self):
  self.assertEqual(v.validate(self.data)['native_records'],1264)
  self.assertEqual(self.data['observation_sequences'][0],self.data['observation_sequences'][1])
 def test_changed_sequence_rejected(self):
  self.data['records'][100]['seq']+=1
  with self.assertRaises(AssertionError):v.validate(self.data)
 def test_missing_sequence_rejected(self):
  del self.data['records'][100]['seq']
  with self.assertRaises((AssertionError,KeyError)):v.validate(self.data)
 def test_repeat_sequence_rejected(self):
  self.data['observation_sequences'][1][10]+=1
  with self.assertRaises(AssertionError):v.validate(self.data)
 def test_alias_rejected(self):
  self.data['captain_aliases'][0][next(iter(self.data['captain_aliases'][0]))]=1
  with self.assertRaises(AssertionError):v.validate(self.data)
 def test_record_alias_rejected(self):
  next(r for r in self.data['records']if r['event']=='captain-subscription')['captain']=1
  with self.assertRaises(AssertionError):v.validate(self.data)
 def test_roster_lost_mask_rejected(self):
  next(r for r in self.data['records']if r['event']=='captain-subscription')['lost']=1
  with self.assertRaises(AssertionError):v.validate(self.data)
if __name__=='__main__':unittest.main()
