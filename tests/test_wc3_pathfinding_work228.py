"""Frozen original flyer-field evidence rejects altered producer/order/sample data."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work228 as V
class Work228Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def row(self,b,e):return next(r for r in b['captures'][0]['rows']if r['event']==e)
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def test_current_bundle_and_literal_header(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['live_samples'],627)
 def test_missing_repeat_or_observer_end(self):
  self.reject(lambda b:b['captures'].pop())
  self.reject(lambda b:self.row(b,'trace-end').update(installed=False))
 def test_control_public_markers_and_ownership_are_frozen(self):
  self.reject(lambda b:b['captures'][2].__setitem__('preload',b['captures'][2]['preload'].replace('x=192.000','x=193.000')))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
  self.reject(lambda b:b['prior_markers'].pop())
 def test_source_pins_and_original_binary_are_required(self):
  self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].pop('work228_observer.js'))
  self.reject(lambda b:b['captures'][0]['rows'][0].update(sha256='0'*64))
 def test_inclusive_rectangle_and_authored_lift_are_frozen(self):
  self.reject(lambda b:self.row(b,'air-raise')['rect'].__setitem__(2,928))
  self.reject(lambda b:self.row(b,'air-raise').update(heightBits=V.bits(128)))
 def test_maximum_and_average_stages_are_frozen(self):
  for e in ('air-initial','air-raise','air-smooth-input','air-smooth-output'):
   self.reject(lambda b:self.row(b,e)['grid']['bits'].__setitem__(102 if e!='air-smooth-output'else 10,0xffffffff))
  self.reject(lambda b:self.row(b,'air-radius').update(value=5))
  self.reject(lambda b:self.row(b,'air-levels').update(value=2))
 def test_live_samples_are_paired_and_individually_checked(self):
  self.reject(lambda b:self.row(b,'air-sample-result').update(resultBits=0))
  self.reject(lambda b:self.row(b,'air-sample-input').update(caller=0x744bc3))
  self.reject(lambda b:b['captures'][0]['rows'].remove(self.row(b,'air-sample-input')))
 def test_signed_zero_peak_order_matches_unchanged_original(self):
  c=self.spec['kernels']['maximum'][17]
  self.assertEqual(V.maximum(c['input'],c['width'],c['height'],c['radius']),c['output'])
  self.assertEqual(c['output'][53],0x80000000)
 def test_truncation_retains_negative_fraction_near_origin(self):
  g=self.spec['kernels']['sample_grid'];point=[-352,224]
  actual=V.sample(g,point);expected=next(r['result']for r in self.spec['kernels']['samples']if r['point']==list(map(V.bits,point)))
  self.assertEqual(actual,expected);self.assertNotEqual(actual,g['bits'][0])
if __name__=='__main__':unittest.main()
