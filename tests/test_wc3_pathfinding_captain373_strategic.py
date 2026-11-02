import copy,gzip,importlib.util,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_captain373_strategic as verifier

class CaptainStrategicContract(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(verifier.SPEC.read_text())
  cls.bundle=json.loads(gzip.decompress(verifier.SPEC.with_suffix('.json.gz').read_bytes()))
  cls.oracle=json.loads(gzip.decompress((ROOT/cls.spec['oracle']).read_bytes()))
 def cases(self):return copy.deepcopy(self.bundle)
 def event(self,cases,event):return next(r for r in cases['regions'][0]['rows']if r.get('event')==event)
 def reject(self,cases):
  with self.assertRaises(ValueError):verifier.runtime(self.spec,cases,self.oracle)
 def test_frozen_observations_control_and_source_pins(self):
  verifier.validate(self.spec)
  self.assertEqual(verifier.runtime(self.spec,self.bundle,self.oracle)['input_words'],13600)
 def test_missing_repeat_rejected(self):
  c=self.cases();c['regions']=c['regions'][:2];self.reject(c)
 def test_instrumented_control_rejected(self):
  c=self.cases();c['regions'][2]['rows'].insert(1,{'event':'module'});self.reject(c)
 def test_incomplete_capture_rejected(self):
  c=self.cases();c['regions'][0]['rows'][-1]['complete']=False;self.reject(c)
 def test_public_marker_changed_rejected(self):
  c=self.cases();self.event(c,'marker')['value']='RSH changed';self.reject(c)
 def test_native_input_class_changed_rejected(self):
  c=self.cases();self.event(c,'region-built')['cells'][1]^=1<<30;self.reject(c)
 def test_native_grid_changed_rejected(self):
  c=self.cases();self.event(c,'region-built')['state']['grid'][0]^=1;self.reject(c)
 def test_native_graph_edge_changed_rejected(self):
  c=self.cases();self.event(c,'region-built')['state']['regions'][7]^=1;self.reject(c)
 def test_native_rng_state_changed_rejected(self):
  c=self.cases();self.event(c,'purpose13-state')['after'][0]^=1;self.reject(c)
 def test_native_destination_changed_rejected(self):
  c=self.cases();self.event(c,'region-random-point')['point'][0]^=1;self.reject(c)
 def test_capture_provenance_changed_rejected(self):
  c=self.cases();c['regions'][0]['rows'][0]['env']='A';self.reject(c)
 def test_hidden_world_owner_tamper_rejected(self):
  c=self.cases();r=next(r for r in c['coexistence'][0]['rows']if r.get('event')=='source-world-retired');r['after']['flags']^=8;self.reject(c)
 def test_target_loss_before_retirement_rejected(self):
  c=self.cases();r=[r for r in c['notification'][0]['rows']if r.get('event')=='source-target-lost'][1];r['flags']^=8;self.reject(c)
 def test_kernel_fixture_reproduces_from_portable_original_input(self):
  import subprocess,tempfile
  with tempfile.TemporaryDirectory()as directory:
   target=Path(directory)/'oracle.h'
   subprocess.run([sys.executable,str(ROOT/'tools/ghidra/research/emit_captain369_regions_test.py'),'--oracle',str(ROOT/self.spec['oracle']),'--output',str(target)],check=True,capture_output=True)
   self.assertEqual(target.read_bytes(),(ROOT/'games/warcraft-3/game/tests/retail_captain369_regions.h').read_bytes())
if __name__=='__main__':unittest.main()
