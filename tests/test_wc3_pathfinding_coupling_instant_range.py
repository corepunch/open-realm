import copy,gzip,json,unittest
from unittest.mock import Mock,patch
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ghidra/research'))
import coupling_instant_range_evidence as v

class RangeTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.bundle=json.loads(gzip.decompress(v.BUNDLE.read_bytes()));cls.files=v.unpack(cls.bundle)
 def test_complete(self):self.assertEqual(v.audit(),dict(observations=2,controls=1,public_records=6900,engine_parity=False,remaining_owner='TARGET-01.2'))
 def mutate(self,event,fn):
  files=copy.deepcopy(self.files);rows=[json.loads(x) for x in files['observe1.jsonl'].decode().splitlines()]
  fn(next(x for x in rows if x['event']==event));files['observe1.jsonl']=('\n'.join(json.dumps(x) for x in rows)+'\n').encode();return files
 def reject(self,event,fn,match):
  with self.assertRaisesRegex(ValueError,match):v.audit_semantics(self.mutate(event,fn))
 def test_raw_range(self):self.reject('target-task',lambda r:r.update(range=0),'raw task range/caller')
 def test_caller(self):self.reject('target-task',lambda r:r.update(caller=0),'raw task range/caller')
 def test_task_code(self):self.reject('target-task',lambda r:r.update(code=0xd0171),'raw task range/caller')
 def test_task_target(self):self.reject('target-task',lambda r:r.update(target='0x0'),'task identities')
 def test_task_incarnation(self):self.reject('target-task',lambda r:r['unit'].update(identity=[0,0]),'task identities')
 def test_cache_range(self):self.reject('instant-producer-enter',lambda r:r['ability']['data'].__setitem__(7,0),'actual targs')
 def test_cache_datab(self):self.reject('instant-producer-enter',lambda r:r['ability']['data'].__setitem__(9,0x3f800000),'actual targs')
 def test_cache_area(self):self.reject('instant-producer-enter',lambda r:r['ability']['data'].__setitem__(6,0),'actual targs')
 def test_rank(self):self.reject('instant-producer-enter',lambda r:r['ability'].update(rank=1),'rank/cache')
 def test_terminal_target(self):self.reject('instant-terminal',lambda r:r['target'].update(identity=[0,0]),'terminal identity')
 def test_retained_target(self):self.reject('instant-producer-leave',lambda r:r['ability'].update(retained=[0,0]),'leave ownership')
 def test_trace_counts(self):self.reject('trace-end',lambda r:r['counts'].update(velocity=0),'trace counts')
 def test_control_hook(self):
  f=copy.deepcopy(self.files);rows=[json.loads(x) for x in f['control1.jsonl'].decode().splitlines()];rows.insert(1,dict(event='velocity'));f['control1.jsonl']=('\n'.join(json.dumps(x) for x in rows)+'\n').encode()
  with self.assertRaisesRegex(ValueError,'unhooked whitelist'):v.audit_semantics(f)
 def test_event_before_own_task(self):
  f=copy.deepcopy(self.files);rows=[json.loads(x) for x in f['observe1.jsonl'].decode().splitlines()]
  a=next(i for i,x in enumerate(rows) if x['event']=='target-task');b=next(i for i,x in enumerate(rows) if x['event']=='marker' and 'issued-target oid=852508' in x['value']);item=rows.pop(b);rows.insert(a,item);f['observe1.jsonl']=('\n'.join(json.dumps(x) for x in rows)+'\n').encode()
  with self.assertRaisesRegex(ValueError,'auto target publication'):v.audit_semantics(f)
 def test_peer_event_after_task(self):
  f=copy.deepcopy(self.files);rows=[json.loads(x) for x in f['observe1.jsonl'].decode().splitlines()]
  a=next(i for i,x in enumerate(rows) if x['event']=='marker' and 'issued-target oid=852507' in x['value']);item=rows.pop(a)
  b=next(i for i,x in enumerate(rows) if x['event']=='target-task' and x['unit']['rawcode']==0x65686970);rows.insert(b+1,item);f['observe1.jsonl']=('\n'.join(json.dumps(x) for x in rows)+'\n').encode()
  with self.assertRaisesRegex(ValueError,'reciprocal publication'):v.audit_semantics(f)
 def test_bounded_archer_head(self):
  f=copy.deepcopy(self.files)
  old='CPI tick=1998 scene=4 label=settled-a ah=852507';new=old.replace('ah=852507','ah=0')
  for name in ('observe1','observe3','control1'):
   key=name+'-preload.txt';self.assertIn(old.encode(),f[key]);f[key]=f[key].replace(old.encode(),new.encode())
   rows=[json.loads(x) for x in f[name+'.jsonl'].decode().splitlines()]
   for row in rows:
    if row['event']=='marker':row['value']=row['value'].replace(old,new)
   rows[-1]['sha256']=v.sha(f[key]);f[name+'.jsonl']=('\n'.join(json.dumps(x) for x in rows)+'\n').encode()
  with self.assertRaisesRegex(ValueError,'bounded direct Archer wait'):v.audit_semantics(f)
 def test_split_field_missing(self):
  f=copy.deepcopy(self.files);f['observe1-preload.txt']=f['observe1-preload.txt'].replace(b' ry=0.000',b'',1)
  with self.assertRaisesRegex(ValueError,'complete state fields'):v.audit_schema(f)
 def test_setup_speed(self):
  f=copy.deepcopy(self.files);f['observe1-preload.txt']=f['observe1-preload.txt'].replace(b'hs=400.000',b'hs=350.000',1)
  with self.assertRaisesRegex(ValueError,'stock speeds'):v.audit_schema(f)
 def test_unit_override(self):
  f=copy.deepcopy(self.files);f['war3map.w3u']=f['war3map.w3u'].replace(b'hfoo',b'earc',1)
  with self.assertRaisesRegex(ValueError,'stock unit override'):v.audit_map(f)
 def test_map_slk_override(self):
  f=copy.deepcopy(self.files);f['(listfile)']+=b'\nUnits/UnitAbilities.slk\n'
  with self.assertRaisesRegex(ValueError,'map archive override'):v.audit_map(f)
 def test_map_trailing_bytes(self):
  f=copy.deepcopy(self.files);f['war3map.w3u']+=b'x'
  with self.assertRaisesRegex(ValueError,'w3u exact end'):v.audit_map(f)
 def test_compressed_digest(self):
  with patch.object(v,'BUNDLE',Mock(read_bytes=lambda:b'changed')):
   with self.assertRaisesRegex(ValueError,'compressed bundle digest'):v.audit()
 def test_literal_digest(self):
  d=copy.deepcopy(self.bundle);d['files']['observe1.jsonl']='eA=='
  with self.assertRaisesRegex(ValueError,'literal digest'):v.audit(d)
 def test_no_engine_parity(self):
  d=copy.deepcopy(self.bundle);d['engine_parity']=True
  with self.assertRaisesRegex(ValueError,'bundle scope'):v.audit(d)

 def boundary_change(self,fn):
  f=copy.deepcopy(self.files);rows=[json.loads(x) for x in f['observe1.jsonl'].decode().splitlines()]
  rs=[x for x in rows if x.get('scene')==6];a=next(i for i,x in enumerate(rs) if x['event']=='instant-producer-enter');b=next(i for i,x in enumerate(rs) if x['event']=='target-task');q=next(x for x in rs[a:b] if x['event']=='target-range-query');fn(q)
  f['observe1.jsonl']=('\n'.join(json.dumps(x) for x in rows)+'\n').encode();return f
 def test_actual_boundary_position(self):
  f=self.boundary_change(lambda q:q['movers'][1]['pose'].__setitem__(2,0))
  with self.assertRaisesRegex(ValueError,'raw boundary pose'):v.audit_boundary(f)
 def test_actual_boundary_radius(self):
  f=self.boundary_change(lambda q:q['movers'][0].update(radius=0x3f800000))
  with self.assertRaisesRegex(ValueError,'raw boundary pose'):v.audit_boundary(f)
 def test_boundary_query_result(self):
  f=self.boundary_change(lambda q:q.update(result=0))
  with self.assertRaisesRegex(ValueError,'boundary query result'):v.audit_semantics(f)
 def test_authored_range_override(self):
  f=copy.deepcopy(self.files);f['war3map.w3a']=f['war3map.w3a'][:-1]
  with self.assertRaisesRegex(ValueError,'exact two authored'):v.audit_semantics(f)

if __name__=='__main__':unittest.main()
