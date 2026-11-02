import copy,gzip,importlib.util,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_modifiers316 as v
class Modifiers316EvidenceTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(v.SPEC.read_text());cls.bundle=json.loads(gzip.decompress(v.SPEC.with_suffix('.json.gz').read_bytes()))
 def reject(self,change):
  spec,bundle=copy.deepcopy(self.spec),copy.deepcopy(self.bundle);change(spec,bundle)
  with self.assertRaises((ValueError,KeyError)):v.validate(spec,bundle)
 def test_complete(self):self.assertEqual(v.validate(self.spec,self.bundle),dict(captures=9,public_markers=918,native_events=10716))
 def test_missing_case(self):self.reject(lambda s,b:b.pop('custom'))
 def test_missing_repeat(self):self.reject(lambda s,b:b['finite'].pop(1))
 def test_missing_control(self):self.reject(lambda s,b:b['reverse'].pop())
 def test_missing_completion(self):self.reject(lambda s,b:b['finite'][0]['rows'][-1].update(complete=False))
 def test_changed_preload(self):self.reject(lambda s,b:b['finite'][2].update(preload=''))
 def test_wrong_environment(self):self.reject(lambda s,b:b['finite'][0]['rows'][0].update(env='A'))
 def test_wrong_source(self):self.reject(lambda s,b:s['pins'].update({v.SOURCES[0]:'0'*64}))
 def test_missing_source(self):self.reject(lambda s,b:s['pins'].pop(v.SOURCES[0]))
 def test_wrong_map(self):self.reject(lambda s,b:b['finite'][0]['rows'][0]['source_sha256'].update(map='0'*64))
 def test_lost_event(self):self.reject(lambda s,b:b['finite'][0]['rows'].pop(20))
 def test_native_fault(self):self.reject(lambda s,b:b['finite'][0]['rows'].insert(-1,{'event':'native-exception'}))
 def test_hooked_control(self):self.reject(lambda s,b:b['finite'][2]['rows'].insert(-1,{'event':'marker','seq':0}))
 def test_getter_word(self):
  def change(s,b):next(r for r in b['finite'][0]['rows']if r.get('event')=='public-speed')['value']^=1
  self.reject(change)
 def test_signed_delta(self):
  def change(s,b):next(r for r in b['finite'][0]['rows']if r.get('event')=='factor-enter')['delta']^=0x80000000
  self.reject(change)
 def test_retained_factor(self):
  def change(s,b):next(r for r in b['custom'][0]['rows']if r.get('event')=='factor-leave')['owner']['factor']^=1
  self.reject(change)
 def test_wrong_owner(self):
  def change(s,b):next(r for r in b['finite'][0]['rows']if r.get('event')=='factor-enter')['caller']='0x0'
  self.reject(change)
 def test_missing_instructions(self):self.reject(lambda s,b:s['instructions'].pop(next(iter(s['instructions']))))
 def test_lower_instruction_count(self):self.reject(lambda s,b:s.update(instruction_count=171))
 def test_wrong_engine_suite(self):self.reject(lambda s,b:s['engine_tests'].pop())
 def test_engine_literal_differs(self):self.reject(lambda s,b:s['cases']['custom']['projection']['factors'][1].__setitem__(2,0))
if __name__=='__main__':unittest.main()
