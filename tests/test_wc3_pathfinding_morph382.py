"""Integrity checks for the complete AEme/BEme frozen native contract."""
import base64,copy,gzip,importlib.util,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_morph382 as v
class Morph382Integrity(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads((ROOT/v.SPEC).read_text());cls.bundle=json.loads(gzip.decompress((ROOT/v.BUNDLE).read_bytes()))
 def group(self,family='ordinary',setting=0):return copy.deepcopy(self.bundle),copy.deepcopy(self.spec['groups'][family+str(setting)]),family,setting
 def mutate(self,family,edit,setting=0):
  bundle,expected,family,setting=self.group(family,setting);stem=v.FAMILIES[family][0];name=stem+str(setting)+'-observe-a.jsonl';rows=[json.loads(x)for x in v.raw(bundle,name).decode().splitlines()];edit(rows);bundle['raw'][name]=base64.b64encode(('\n'.join(json.dumps(r)for r in rows)+'\n').encode()).decode();return bundle,expected,family,setting
 def rejects(self,mutant):
  with self.assertRaises((ValueError,KeyError,StopIteration)):v.check_group(*mutant)
 def test_complete_inventory(self):
  self.assertEqual(v.runtime(self.bundle,self.spec),dict(captures=36,scenes=38,public_markers=3790,projected_events=8470,native_events=4656,recorded_system_exceptions=24))
 def test_complete_source_and_original_bundle_contract(self):v.validate(self.spec,ROOT)
 def test_missing_raw_file(self):
  b=copy.deepcopy(self.bundle);del b['raw']['0-observe-a.jsonl']
  with self.assertRaises(ValueError):v.runtime(b,self.spec)
 def test_missing_diagnostic_is_not_accepted(self):
  b=copy.deepcopy(self.bundle);del b['raw']['fresh-caster-sleep-pause-0-observe-a.jsonl']
  with self.assertRaises(ValueError):v.runtime(b,self.spec)
 def test_removed_source_pin(self):
  s=copy.deepcopy(self.spec);del s['pins']['tools/frida/research/payoff382w_observer.js']
  with self.assertRaises(ValueError):v.validate(s,ROOT)
 def test_engine_pool_gate_cannot_be_dropped(self):
  s=copy.deepcopy(self.spec);s['engine_tests']=s['engine_tests'][1:]
  with self.assertRaises(ValueError):v.validate(s,ROOT)
 def test_unknown_native_event(self):self.rejects(self.mutate('ordinary',lambda rows:rows.insert(2,dict(event='unknown-native-event',seq=999,tick=0))))
 def test_missing_complete_footer(self):self.rejects(self.mutate('ordinary',lambda rows:rows.pop()))
 def test_missing_native_end(self):self.rejects(self.mutate('ordinary',lambda rows:rows.pop(next(i for i,r in enumerate(rows)if r['event']=='trace-end'))))
 def test_extra_startup_exception(self):self.rejects(self.mutate('ordinary',lambda rows:rows.insert(2,copy.deepcopy(next(r for r in rows if r['event']=='native-exception')))))
 def test_literal_exception_trace(self):
  def edit(rows):next(r for r in rows if r['event']=='native-exception')['trace'][0]='0x0'
  self.rejects(self.mutate('ordinary',edit))
 def test_policy_zero_is_not_one(self):
  def edit(rows):next(r for r in rows if r['event']=='morph-policy-leave')['result']=1
  self.rejects(self.mutate('ordinary',edit))
 def test_exact_primary_transition(self):
  def edit(rows):next(r for r in rows if r['event']=='transition-timer')['delay']+=1
  self.rejects(self.mutate('ordinary',edit))
 def test_exact_private_lifetime(self):
  def edit(rows):next(r for r in rows if r['event']=='metamorphosis-buff-attach-enter')['duration']+=1
  self.rejects(self.mutate('ordinary',edit))
 def test_alias_concrete_class(self):
  def edit(rows):next(r for r in rows if r['event']=='metamorphosis-buff-attach-leave')['availability']['class']='000000'
  self.rejects(self.mutate('alias',edit))
 def test_alias_source_identity(self):
  def edit(rows):next(r for r in rows if r['event']=='metamorphosis-buff-attach-enter')['source']['code']=0x41456d65
  self.rejects(self.mutate('alias',edit))
 def test_clock_resume_while_physical_sleep_held(self):
  def edit(rows):next(r for r in rows if r['event']=='private-clock-resume-enter')['state']['pauseDepth']=0
  self.rejects(self.mutate('sleep',edit))
 def test_private_clock_depth_is_literal(self):
  def edit(rows):next(r for r in rows if r['event']=='private-clock-resume-enter')['availability']['clockDepth']=1
  self.rejects(self.mutate('sleep',edit))
 def test_public_removal_inverse_is_synchronous(self):
  def edit(rows):next(r for r in rows if r['event']=='morph-form-enable-leave')['tick']+=1
  self.rejects(self.mutate('public',edit))
 def test_new_scalar_word_cannot_be_dropped(self):
  def edit(rows):next(r for r in rows if isinstance(r.get('state'),dict))['state']['unrecognizedScalar']=17
  self.rejects(self.mutate('ordinary',edit))
 def test_unit118_is_literal(self):
  def edit(rows):next(r['state']for r in rows if 'invisibilityScalar'in r.get('state',{}))['invisibilityScalar'][0]^=1
  self.rejects(self.mutate('ordinary',edit))
 def test_unit124_is_literal(self):
  def edit(rows):next(r['state']for r in rows if 'invisibilityScalar'in r.get('state',{}))['invisibilityScalar'][3]^=1
  self.rejects(self.mutate('ordinary',edit))
 def test_missing_scalar_word(self):
  def edit(rows):next(r['state']for r in rows if 'invisibilityScalar'in r.get('state',{}))['invisibilityScalar'].pop()
  self.rejects(self.mutate('ordinary',edit))
 def test_source_root_relocation_is_the_only_builder_change(self):
  name=self.spec['ported_builders'][0];captured=base64.b64decode(self.bundle['captured_sources'][name],validate=True);live=(ROOT/name).read_bytes();self.assertEqual(v.builder_ast(captured),v.builder_ast(live))
  self.assertNotEqual(v.builder_ast(captured),v.builder_ast(live+b'\nUNRELATED_CHANGE=1\n'))
 def test_unproven_parent_grammar(self):
  with self.assertRaises(ValueError):v.builder_ast('PARENT=arbitrary_source()\n')
 def test_only_the_two_proven_pointer_offsets(self):self.assertEqual(v.CONTRACT['pointer_unit_offsets'],[0x128,0x12c]);self.assertEqual(v.CONTRACT['literal_unit_offsets'],[0x118,0x11c,0x120,0x124])
 def test_live_pointer_replacement_requires_observed_zero(self):
  rows=[dict(event='probe',seq=i,tick=i,state=dict(identity=[1,2],invisibilityScalar=[0,0,0,0,pointer,0]))for i,pointer in enumerate([123,124,0],1)]
  with self.assertRaises(ValueError):v.project(rows)
 def test_reuse_after_last_holder_zero_gets_new_token(self):
  rows=[dict(event='probe',seq=i,tick=i,state=dict(identity=[1,2],invisibilityScalar=[0,0,0,0,pointer,0]))for i,pointer in enumerate([123,0,123,0],1)]
  projection,ledger=v.project(rows);self.assertEqual([projection[i]['state']['invisibilityScalar'][4][1]for i in(0,2)],[1,2]);self.assertEqual([r['token']for r in ledger],[1,2])
 def test_concurrent_alias_cannot_be_retired_early(self):
  rows=[dict(event='probe',seq=i,tick=i,state=dict(identity=identity,invisibilityScalar=[0,0,0,0,pointer,0]))for i,(identity,pointer)in enumerate([([1,2],123),([3,4],123),([1,2],0),([1,2],123),([1,2],0),([3,4],0)],1)]
  projection,ledger=v.project(rows);self.assertEqual([projection[i]['state']['invisibilityScalar'][4][1]for i in(0,1,3)],[1,1,1]);self.assertEqual(len(ledger),1)
if __name__=='__main__':unittest.main()
