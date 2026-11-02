import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_clock275 as v
class ClockContract(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def test_repeated_public_and_control(self):self.assertTrue(v.validate_runtime(self.bundle,self.spec)['pause_frozen'])
 def test_original_literal_header(self):self.assertEqual(v.header(self.bundle['original']),v.HEADER.read_text())
 def test_reject_rescaled_primary_step(self):
  rows=copy.deepcopy(self.bundle['captures'][0]['rows']);next(r for r in rows if r['event']=='marker'and r['primary'])['inputs']={'1065353216':21}
  with self.assertRaises(ValueError):v.check_events(rows)
 def test_reject_wrong_speed(self):
  rows=copy.deepcopy(self.bundle['captures'][0]['rows']);next(r for r in rows if r['event']=='speed'and r['input']==3)['canonical']=3
  with self.assertRaises(ValueError):v.check_events(rows)
 def test_reject_missing_domain(self):
  rows=copy.deepcopy(self.bundle['captures'][0]['rows']);[r['selectors'].__setitem__(1,0)for r in rows if r['event']=='marker']
  with self.assertRaises(ValueError):v.check_events(rows)
 def test_reject_host_quantum(self):
  rows=copy.deepcopy(self.bundle['captures'][0]['rows']);next(r for r in rows if r['event']=='host'and r['after']>r['before'])['after']+=1
  with self.assertRaises(ValueError):v.check_events(rows)
 def test_reject_inactive_host_rates(self):
  rows=copy.deepcopy(self.bundle['captures'][0]['rows'])
  for r in rows:
   if r['event']=='host'and r['rate']==0:r['after']=r['before']
  with self.assertRaises(ValueError):v.check_events(rows)
 def test_reject_pause_drift(self):
  rows=copy.deepcopy(self.bundle['captures'][0]['rows']);pauses=[r for r in rows if r['event']=='pause'];start,end=pauses[-2:]
  next(r for r in rows if r['event']=='heartbeat'and start['wall']+100<r['wall']<end['wall']-100)['clocks']['presentation']['time']+=1
  with self.assertRaises(ValueError):v.check_events(rows)
 def test_reject_control_perturbation(self):
  bundle=copy.deepcopy(self.bundle);bundle['captures'][2]['preload']=bundle['captures'][2]['preload'].replace('x=256.000','x=257.000',1)
  with self.assertRaises(ValueError):v.validate_runtime(bundle,self.spec)
if __name__=='__main__':unittest.main()
