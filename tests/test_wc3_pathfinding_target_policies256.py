"""Target-policy closure must retain all cases and the actual resume owner."""
import copy,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_target_policies256 as v
class PolicyTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.spec=json.loads(v.FIXTURE.read_text())
 def test_complete_policy_join(self):self.assertEqual(len(v.validate(self.spec)['coverage']),22)
 def test_missing_policy_fails(self):
  s=copy.deepcopy(self.spec);s['coverage'].pop(12)
  with self.assertRaises(ValueError):v.validate(s)
 def test_changed_retail_outcome_fails(self):
  s=copy.deepcopy(self.spec);s['coverage'][12]['outcome']['kind']='cancelled'
  with self.assertRaises(ValueError):v.validate(s)
 def test_unimplemented_case_fails(self):
  s=copy.deepcopy(self.spec);s['coverage'][2]['engine']=[]
  with self.assertRaises(ValueError):v.validate(s)
 def test_unpinned_sources_fail(self):
  s=copy.deepcopy(self.spec);s['pins']={}
  with self.assertRaises(ValueError):v.validate(s)
 def test_resumed_owner_is_real_and_precedes_follower(self):
  for rows in self.spec['pause_rows']:self.assertEqual(v.pause_resume(rows),self.spec['pause_resume'])
 def test_missing_resumed_owner_fails(self):
  rows=copy.deepcopy(self.spec['pause_rows'][0]);del rows[next(i for i,r in enumerate(rows)if r.get('event')=='gtick'and r['c']==9491)]
  with self.assertRaises(ValueError):v.pause_resume(rows)
 def test_new_owner_after_follower_fails(self):
  rows=copy.deepcopy(self.spec['pause_rows'][0]);i=next(i for i,r in enumerate(rows)if r.get('event')=='gtick'and r['c']==9491)
  rows[i],rows[i+1]=rows[i+1],rows[i]
  with self.assertRaises(ValueError):v.pause_resume(rows)
 def test_pause_does_not_publish_target_lost(self):
  rows=copy.deepcopy(self.spec['pause_rows'][0]);rows.insert(1,dict(event='target-lost'))
  with self.assertRaises(ValueError):v.pause_resume(rows)
 def test_retired_owner_cannot_keep_moving(self):
  rows=copy.deepcopy(self.spec['pause_rows'][0]);r=next(r for r in rows if r.get('event')=='gtick'and r['c']==9291 and r['target']==[0xffffffff]*2);r['members'][0]['vel']=[1,1]
  with self.assertRaises(ValueError):v.pause_resume(rows)
if __name__=='__main__':unittest.main()
