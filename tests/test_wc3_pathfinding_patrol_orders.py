"""Frozen Patrol ownership claims reject altered decisions and unmatched inputs."""
import copy,gzip,importlib.util,json,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class PatrolOrderEvidenceTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  spec=importlib.util.spec_from_file_location('patrol176_claims',ROOT/'tools/frida/research/order0118_verify.py')
  cls.claims=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.claims)
  cls.frozen=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/research/ORDER-01.18-expected.json.gz').read_bytes()))
 def test_complete_original_ownership_decisions(self):self.assertEqual(self.claims.check(self.frozen),[])
 def test_threshold_combat_acquisition_rotation_and_control_mutations_fail(self):
  for field in('threshold','combat','acquisition','rotation','control'):
   data=copy.deepcopy(self.frozen)
   if field=='threshold':data['scenes']['p2']['timelines']['3']['transitions'].append(dict(tick=1,order=851991))
   elif field=='combat':data['scenes']['p2']['timelines']['0']['damage'][0]['source_order']=851983
   elif field=='acquisition':data['scenes']['p2']['decisions']['0']=[r for r in data['scenes']['p2']['decisions']['0']if r['tick']!=22]
   elif field=='rotation':data['scenes']['p1r2']['decisions']['4']=[r for r in data['scenes']['p1r2']['decisions']['4']if r.get('caller')!='6f5fe0b8']
   else:data['scenes']['p2']['public_identical']=False
   with self.subTest(field=field):self.assertTrue(self.claims.check(data))
 def test_queue_input_witnesses_are_not_mislabeled_as_repeats(self):
  for name,scene in self.frozen['scenes'].items():
   self.assertEqual(len(scene['captures']),2 if name=='p2'else 1)
   self.assertEqual(len(scene['controls']),1 if name=='p2'else 0)

if __name__=='__main__':unittest.main()
