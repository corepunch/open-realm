import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_refresh274 as v
class RefreshProducer(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()));cls.events=v.normalized(cls.bundle['captures'][0]['rows'])
 def test_repeats_and_control(self):
  r=v.validate_runtime(self.bundle,self.spec);self.assertEqual(r['ordinary_intervals'],195);self.assertTrue(r['live_extra_next_sample_observed']);self.assertEqual(r['extra_intervals'],8)
 def test_complete_original_literal_header(self):
  self.assertEqual(v.header(self.bundle['original']),v.HEADER.read_text());self.assertEqual(len(self.bundle['original']['prediction']),180)
 def test_reject_missing_extra_bit(self):
  events=copy.deepcopy(self.events);next(r for r in events if r['event']=='refresh'and r['before']['cd']==-1 and r['flags']&0x400)['flags']^=0x400
  with self.assertRaises(ValueError):v.check_events(events)
 def test_reject_shortened_countdown(self):
  events=copy.deepcopy(self.events);next(r for r in events if r['event']=='refresh'and r['before']['cd']>0)['cd']-=1
  with self.assertRaises(ValueError):v.check_events(events)
 def test_reject_changed_producer(self):
  events=copy.deepcopy(self.events);next(r for r in events if r['event']=='extra-producer')['caller']+=1
  with self.assertRaises(ValueError):v.check_events(events)
 def test_reject_changed_extra_sample_counter(self):
  events=copy.deepcopy(self.events);next(r for r in events if r['event']=='refresh'and r['c']==1607 and r['flags']&0x400)['c']-=1
  with self.assertRaises(ValueError):v.check_events(events)
 def test_reject_incomplete_or_perturbed_control(self):
  for mode in('incomplete','perturbed'):
   bundle=copy.deepcopy(self.bundle)
   if mode=='incomplete':bundle['captures'][0]['rows'][-1]['complete']=False
   else:bundle['captures'][2]['preload']=bundle['captures'][2]['preload'].replace('R274 tick=1 label=start','R274 tick=999 label=start',1)
   with self.assertRaises(ValueError):v.validate_runtime(bundle,self.spec)
if __name__=='__main__':unittest.main()
