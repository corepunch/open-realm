"""Reject stale, incomplete or grammar-inconsistent original source evidence."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_numeric259 as v
class NumericSourceTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.s=json.loads(v.FIXTURE.read_text());cls.b=json.loads(gzip.decompress(v.BUNDLE.read_bytes()))
 def test_complete_original_contract(self):self.assertEqual(v.grammar(v.validate(self.s)['original'])['dfa_edges'],4096)
 def test_missing_state_fails(self):
  b=copy.deepcopy(self.b['original']);b['numeric_states'].pop()
  with self.assertRaises(ValueError):v.grammar(b)
 def test_changed_transition_fails(self):
  b=copy.deepcopy(self.b['original']);b['numeric_states'][0]['transitions'][ord('.')]=231
  with self.assertRaises(ValueError):v.grammar(b)
 def test_changed_octals_fail(self):
  b=copy.deepcopy(self.b['original']);next(r for r in b['rows']if r['input']=='078')['text']='078'
  with self.assertRaises(ValueError):v.grammar(b)
 def test_missing_repetition_fails(self):
  b=copy.deepcopy(self.b);b['captures'].pop()
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_instrumented_control_fails(self):
  b=copy.deepcopy(self.b);next(c for c in b['captures']if c['rows'][0]['mode']=='control')['rows'].insert(1,dict(event='module'))
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_invalid_source_requires_diagnostic(self):
  b=copy.deepcopy(self.b);c=next(c for c in b['captures']if c['name']=='bad0');c['rows']=[r for r in c['rows']if r.get('event')!='compile-error']
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_identifiers_cannot_be_replaced_by_reals(self):
  b=copy.deepcopy(self.b);next(r for r in b['captures'][0]['rows']if r.get('text')=='nan')['token']=265
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_changed_public_control_fails(self):
  b=copy.deepcopy(self.b);next(c for c in b['captures']if c['rows'][0]['mode']=='control')['preload']=''
  with self.assertRaises(ValueError):v.runtime(b,self.s)
 def test_unpinned_sources_fail(self):
  s=copy.deepcopy(self.s);s['pins']={}
  with self.assertRaises(ValueError):v.validate(s)
if __name__=='__main__':unittest.main()
