"""Original selected queued heads precede tasks, including nested callback edges."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work250 as v
class Work250Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def test_repeated_native_order(self):
  result=v.validate_runtime(self.bundle,self.spec)
  self.assertEqual(result['captures'],4);self.assertEqual(result['public_markers'],994)
 def test_sources_are_pinned(self):self.assertEqual(len(v.validate(self.spec)['captures']),4)
 def test_rejects_enqueue_event(self):
  e=copy.deepcopy(self.spec['events'][0]);row=next(r for r in e if r.get('unit')==0 and r.get('tick')==81 and r['event']=='issued-point-enter');row['tick']=29
  with self.assertRaises(ValueError):v.verify_order(e,False)
 def test_rejects_task_before_event(self):
  e=copy.deepcopy(self.spec['events'][0]);next(r for r in e if r['event']=='issued-point-enter')['internal']=True
  with self.assertRaises(ValueError):v.verify_order(e,False)
 def test_rejects_nested_prepared_affinity(self):
  e=copy.deepcopy(self.spec['events'][1]);[r for r in e if r['event']=='bind'][1]['flags']=14
  with self.assertRaises(ValueError):v.verify_order(e,True)
 def test_rejects_stop_canceling_outer_task(self):
  e=[r for r in copy.deepcopy(self.spec['events'][1])if not(r['event']=='point-task'and r['tick']==81)]
  with self.assertRaises(ValueError):v.verify_order(e,True)
 def test_rejects_truncated_capture(self):
  for field,value in [('complete',False),('markers',249)]:
   b=copy.deepcopy(self.bundle);b['captures'][2]['rows'][-1][field]=value
   with self.assertRaises(ValueError):v.validate_runtime(b,self.spec)
 def test_rejects_wrong_map_or_observer(self):
  for key in('map','work250_observer.js'):
   b=copy.deepcopy(self.bundle);b['captures'][0]['rows'][0]['source_sha256'][key]='0'*64
   with self.assertRaises(ValueError):v.validate_runtime(b,self.spec)
if __name__=='__main__':unittest.main()
