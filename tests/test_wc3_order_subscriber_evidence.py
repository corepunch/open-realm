"""Retail subscription claims reject changed ordering and incomplete runs."""
import copy,importlib.util,json,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class SubscriberEvidenceTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  spec=importlib.util.spec_from_file_location('order177_claims',ROOT/'tools/ghidra/research/verify_order177_subscribers.py')
  cls.verify=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.verify)
  cls.data=json.loads((ROOT/'tools/ghidra/fixtures/research/ORDER-03.1-expected.json').read_bytes())
 def test_all_public_claims(self):self.assertEqual(self.verify.claims(self.data),[])
 def test_changed_delivery_synchronous_counts_repeat_or_control_fails(self):
  for mutation in('delivery','synchronous','counts','repeat','control'):
   data=copy.deepcopy(self.data);rows=data['live']['controls']['forward']
   if mutation=='delivery':rows[:]=[s.replace('enter trig=T4','enter trig=T5')for s in rows]
   elif mutation=='synchronous':rows[:]=[s.replace('issue end ','late return ')for s in rows]
   elif mutation=='counts':rows[:]=[s.replace('eval=0 exec=0','eval=2 exec=2')for s in rows]
   elif mutation=='repeat':data['live']['repeat']['forward-observe-1 vs 2 (CUnit destructor rows excluded)']=False
   else:data['live']['observer_equals_control']['forward-observe-1.jsonl']=False
   with self.subTest(mutation=mutation):self.assertTrue(self.verify.claims(data))
 def test_capture_requires_matching_binary_completion_and_artifacts(self):
  rows=[dict(event='metadata',sha256=self.verify.BINARY_SHA,mode='observe'),dict(event='trace-end',installed=True),dict(event='artifacts')]
  raw=lambda x:('\n'.join(json.dumps(r)for r in x)+'\n').encode()
  b=raw(rows);self.assertEqual(self.verify.check_capture(b,self.verify.digest(b),'observe'),3)
  for mutation in('binary','completion','artifacts','error'):
   changed=copy.deepcopy(rows)
   if mutation=='binary':changed[0]['sha256']='0'*64
   elif mutation=='completion':changed.pop(1)
   elif mutation=='artifacts':changed.pop(2)
   else:changed.append(dict(event='script-error'))
   b=raw(changed)
   with self.subTest(mutation=mutation),self.assertRaises(AssertionError):self.verify.check_capture(b,self.verify.digest(b),'observe')
if __name__=='__main__':unittest.main()
