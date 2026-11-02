"""Reject confused float/flight affinity and altered public selected groups."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work237 as V
class Work237Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def attachment(self,b):return next(r for r in b['captures'][0]['rows']if r.get('event')=='attach'and r['type']==2)
 def test_frozen_contract(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['ordinary_cohorts'],2)
 def test_missing_repeat(self):self.reject(lambda b:b['captures'].pop())
 def test_confused_float_and_flight(self):self.reject(lambda b:self.attachment(b).update(type=16))
 def test_wrong_option(self):self.reject(lambda b:self.attachment(b).update(options=1))
 def test_wrong_request(self):
  def change(b):
   row=next(r for r in b['captures'][0]['rows']if r.get('event')=='retain');row['wrapper']='0x'+format(self.attachment(b)['requests'][1],'x')
  self.reject(change)
 def test_dropped_bound_member(self):
  self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='bind'and r['depth']==0)['members'].pop())
 def test_wrong_flags(self):self.reject(lambda b:next(r for r in b['captures'][0]['rows']if r.get('event')=='bind'and r['depth']==0).update(flags=14))
 def test_incomplete_or_unowned(self):
  self.reject(lambda b:b['captures'][0]['rows'][-1].update(complete=False))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
 def test_native_domains(self):
  rows=self.spec['kernel']['rows'];self.assertEqual(len(rows),288)
  def case(bits,special=0,forced=0,alt=0,present=7):return next(r['attached']for r in rows if (r['bits'],r['special'],r['forced'],r['alt'],r['present'])==(bits,special,forced,alt,present))
  self.assertEqual(case(1),[0]);self.assertEqual(case(2),[0]);self.assertEqual(case(16),[1])
  self.assertEqual(case(2,1,0,1),[2]);self.assertEqual(case(2,1,1,1),[0]);self.assertEqual(case(16,1,0,1),[1])
if __name__=='__main__':unittest.main()
