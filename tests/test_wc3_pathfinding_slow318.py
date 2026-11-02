import copy,gzip,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_slow318 as v
class Evidence(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.SPEC.with_suffix('.json.gz').read_bytes()))
 def test_valid(self):self.assertEqual(v.validate(self.spec,self.bundle)['motion_rows'],834)
 def reject(self,mutate):
  s,b=copy.deepcopy(self.spec),copy.deepcopy(self.bundle);mutate(s,b)
  with self.assertRaises((ValueError,KeyError)):v.validate(s,b,False)
 def test_missing_control(self):self.reject(lambda s,b:b['morph'].pop())
 def test_missing_scene(self):self.reject(lambda s,b:b.pop('preslow'))
 def test_sources(self):self.reject(lambda s,b:s['pins'].clear())
 def test_engine(self):self.reject(lambda s,b:s['engine_tests'].clear())
 def test_instructions(self):self.reject(lambda s,b:s['instructions'].popitem())
 def test_state(self):self.reject(lambda s,b:s['cases']['preslow']['projection']['motion'][25].__setitem__(4,0))
 def test_clock(self):self.reject(lambda s,b:s['cases']['morph']['projection']['motion'][25].__setitem__(1,0))
 def test_isolation(self):self.reject(lambda s,b:s['cases']['morph']['projection']['isolation'][0].__setitem__(5,9))
 def test_turn(self):self.reject(lambda s,b:s['cases']['morph']['turn'][0].__setitem__(5,0))
 def test_limits(self):self.reject(lambda s,b:s['cases']['morph']['profile_limits'][0].__setitem__(5,0))
 def test_map(self):self.reject(lambda s,b:b['morph'][0]['rows'][0]['source_sha256'].__setitem__('map','bad'))
 def test_owner(self):self.reject(lambda s,b:b['morph'][0]['rows'][0].__setitem__('owned',False))
 def test_incomplete(self):self.reject(lambda s,b:b['morph'][0]['rows'][-1].__setitem__('complete',False))
 def test_preload(self):self.reject(lambda s,b:b['morph'][0].__setitem__('preload',''))
 def test_live_event(self):
  def mutate(s,b):
   rows=b['morph'][0]['rows'];actor=next(r['m']['id']for r in rows if r.get('event')=='physical-radius'and r['m']['pos']==[0x41c00000,0x41800000])
   member=next(m for r in rows if r.get('event')=='commit-leave'for m in r['g']['members']if m['id']==actor and m['m'])
   member['m']['pos'][0]^=1
  self.reject(mutate)
 def test_fault(self):self.reject(lambda s,b:b['morph'][0]['rows'].insert(-1,{'event':'native-exception'}))
 def test_bind(self):self.reject(lambda s,b:s['cases']['preslow']['projection']['binds'][0].__setitem__(6,2))
 def test_missing_event(self):self.reject(lambda s,b:b['morph'][0]['rows'].pop(10))
if __name__=='__main__':unittest.main()
