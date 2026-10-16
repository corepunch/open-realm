"""Frozen retail mesh evidence must reject altered geometry, queries and controls."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_work229 as V
class Work229Tests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def row(self,b,e):return next(r for r in b['captures'][0]['rows']if r.get('event')==e)
 def reject(self,change):
  b=copy.deepcopy(self.bundle);change(b)
  with self.assertRaises((ValueError,KeyError)):V.validate_runtime(b,self.spec)
 def test_literal_header_and_runtime(self):
  V.validate(self.spec);self.assertEqual(V.validate_runtime(self.bundle,self.spec)['unique_queries'],383)
 def test_missing_repeat_or_completion(self):
  self.reject(lambda b:b['captures'].pop())
  self.reject(lambda b:self.row(b,'trace-end').update(groups=1))
 def test_observer_ownership_and_source_are_required(self):
  self.reject(lambda b:b['captures'][0]['rows'][0].update(owned=False))
  self.reject(lambda b:b['captures'][0]['rows'][0]['source_sha256'].pop('work229_observer.js'))
 def test_original_geometry_matrix_and_transformed_words_are_frozen(self):
  for key in ('vertices','matrices','transformed','indices'):
   self.reject(lambda b:self.row(b,'mesh-group')[key].__setitem__(0,0xffffffff))
 def test_original_primitive_and_intersection_results_are_frozen(self):
  self.reject(lambda b:self.row(b,'mesh-group').update(primitive=4))
  self.reject(lambda b:self.row(b,'mesh-group').update(distance=0))
 def test_each_distinct_live_query_is_frozen(self):
  def change(b):
   r=next(r for r in b['captures'][0]['rows'][20:]if r.get('event')=='deck'and r['hit']);r['height']^=1
  self.reject(change)
 def test_control_public_markers_are_frozen(self):
  self.reject(lambda b:b['captures'][2].__setitem__('preload',b['captures'][2]['preload'].replace('x=192.000','x=193.000')))
if __name__=='__main__':unittest.main()
