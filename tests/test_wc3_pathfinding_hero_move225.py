"""Reject incomplete or numerically altered original Hero movement evidence."""
import copy,gzip,json,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_hero_move225 as V
class HeroMoveContractTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.spec=json.loads(V.FIXTURE.read_text());cls.bundle=json.loads(gzip.decompress(V.BUNDLE.read_bytes()))
 def row(self,b,event,map='fraction'):
  return next(r for c in b['captures']if c['map']==map for r in c['rows']if r['event']==event)
 def reject(self,fn):
  b=copy.deepcopy(self.bundle);fn(b)
  with self.assertRaises(ValueError):V.validate_runtime(b,self.spec)
 def test_original_repeats_and_unhooked_controls(self):
  self.assertEqual(V.validate_runtime(self.bundle,V.validate(self.spec)),dict(captures=6,controls=3,maps=3,public_markers=675,native_events=2100))
 def test_every_source_and_bundle_pin(self):
  for p in V.SOURCES:
   s=copy.deepcopy(self.spec);s['pins'][p]='0'*64
   with self.assertRaises(ValueError):V.validate(s)
  s=copy.deepcopy(self.spec);s['bundle_sha256']='0'*64
  with self.assertRaises(ValueError):V.validate(s)
 def test_all_original_instruction_bytes_required(self):
  s=copy.deepcopy(self.spec);s['instructions'].pop(next(iter(s['instructions'])))
  with self.assertRaises(ValueError):V.validate(s)
 def test_every_native_producer_is_frozen(self):
  for event in V.EVENTS:
   def change(b):
    r=self.row(b,event);key=next(k for k in ('result','after','delta','maximum')if k in r);r[key]^=1
   self.reject(change)
 def test_caller_and_order_are_preserved(self):
  self.reject(lambda b:self.row(b,'hero-refresh').update(caller='000000'))
  self.reject(lambda b:self.row(b,'move-additive-delta').update(seq=0))
 def test_owner_profile_and_flags_are_preserved(self):
  self.reject(lambda b:self.row(b,'hero-contribution')['unit'].update(rawcode=0))
  self.reject(lambda b:self.row(b,'hero-contribution')['unit'].update(flags=0xffffffff))
 def test_fractional_default_is_not_current_speed(self):
  s=self.spec['maps']['fraction']['stages'][1]
  self.assertEqual(s['default'],0x438e9999);self.assertEqual(s['current'],0x438e9998)
  self.assertNotEqual(s['default'],s['current'])
 def test_control_cannot_be_instrumented(self):
  self.reject(lambda b:b['captures'][2]['rows'].insert(1,dict(event='module',seq=1)))
 def test_every_map_and_complete_marker_required(self):
  self.reject(lambda b:b['captures'].pop())
  for n in range(9):
   self.reject(lambda b:b['captures'][n]['rows'][-1].update(complete=False))
   self.reject(lambda b:b['captures'][n].__setitem__('preload',b['captures'][n]['preload'].replace('nonhero=270.000','nonhero=0.000')))
 def test_runtime_addresses_are_not_frozen(self):
  b=copy.deepcopy(self.bundle)
  for c in b['captures']:
   for r in c['rows']:
    if 'hero'in r:r['hero']='0x12340000'
    if 'handle'in r:r['handle']=123
    if r.get('unit'):r['unit']['identity']=[123,456]
  self.assertEqual(V.validate_runtime(b,self.spec)['maps'],3)
 def test_engine_words_checked_against_native_fixture(self):
  lines=[]
  for p,map in enumerate(('custom','fraction')):
   for n,s in enumerate(self.spec['maps'][map]['stages'][:9]):
    lines.append(f"M225 engine profile={p} stage={n} agi={s['agility']} item={s['agility']-s['base_agility']} default={s['default']:08x} current={s['current']:08x} cached=00000000")
  text='\n'.join(lines);self.assertEqual(V.validate_engine_words(text,self.spec),18)
  for bad in (text.replace('current=438e9998','current=438e9999'),text.split('\n',1)[1]):
   with self.assertRaises(ValueError):V.validate_engine_words(bad,self.spec)
if __name__=='__main__':unittest.main()
