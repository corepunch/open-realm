import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_captain320 as v
class Evidence(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.SPEC.with_suffix('.json.gz').read_bytes()))
 def test_valid(self):self.assertEqual(v.validate(self.spec,self.bundle)['actor_rows'],293)
 def reject(self,mutate):
  s,b=copy.deepcopy(self.spec),copy.deepcopy(self.bundle);mutate(s,b)
  with self.assertRaises((ValueError,KeyError)):v.validate(s,b,False)
 def test_missing_control(self):self.reject(lambda s,b:b.pop())
 def test_missing_repeat(self):self.reject(lambda s,b:b.pop(1))
 def test_sources(self):self.reject(lambda s,b:s['pins'].clear())
 def test_engine(self):self.reject(lambda s,b:s['engine_tests'].clear())
 def test_instructions(self):self.reject(lambda s,b:(s['instructions'].popitem(),s.__setitem__('instruction_count',930)))
 def test_state(self):self.reject(lambda s,b:s['projection']['motion'][25].__setitem__(4,0))
 def test_actor(self):self.reject(lambda s,b:s['projection']['actor'][33].__setitem__(3,0x41068b79))
 def test_clock(self):self.reject(lambda s,b:s['projection']['motion'][25].__setitem__(1,0))
 def test_shared(self):self.reject(lambda s,b:s['projection']['shared'][30].__setitem__(2,0x40ad0000))
 def test_map(self):self.reject(lambda s,b:b[0]['rows'][0]['source_sha256'].__setitem__('map','bad'))
 def test_owner(self):self.reject(lambda s,b:b[0]['rows'][0].__setitem__('owned',False))
 def test_incomplete(self):self.reject(lambda s,b:b[0]['rows'][-1].__setitem__('complete',False))
 def test_preload(self):self.reject(lambda s,b:b[6].__setitem__('preload',''))
 def test_fault(self):self.reject(lambda s,b:b[0]['rows'].insert(-1,{'event':'native-exception'}))
 def test_missing_event(self):self.reject(lambda s,b:b[0]['rows'].pop(10))
 def test_notification_flags(self):self.reject(lambda s,b:next(r for r in b[2]['rows']if r.get('event')=='attack-notify')['packet'].__setitem__(3,0))
 def test_notification_owner(self):self.reject(lambda s,b:next(r for r in b[2]['rows']if r.get('event')=='attack-notify')['trace'].__setitem__(2,'0x0'))
 def test_notification_clock(self):self.reject(lambda s,b:next(r for r in b[2]['rows']if r.get('event')=='attack-cap-begin')['primary'].__setitem__(0,0))
 def test_actor_turn(self):self.reject(lambda s,b:next(r for r in b[4]['rows']if r.get('event')=='bind'and r['m']['radius']==0)['m'].__setitem__('turn',0x3ecccccd))
if __name__=='__main__':unittest.main()
