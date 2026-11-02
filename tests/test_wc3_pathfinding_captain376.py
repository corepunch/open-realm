"""Semantic integrity checks for complete Captain376 native observations."""
from pathlib import Path
import base64,copy,gzip,hashlib,importlib.util,json,tempfile,unittest,struct
ROOT=Path(__file__).resolve().parents[1]
SPEC=ROOT/'tools/ghidra/fixtures/retail-captain376-native-1.27.json.gz'
loader=importlib.util.spec_from_file_location('captain376_replay',ROOT/'tools/ghidra/research/captain376_native_replay.py')
v=importlib.util.module_from_spec(loader);loader.loader.exec_module(v)
def update(b,name,raw):b['files'][name]={'sha256':hashlib.sha256(raw).hexdigest(),'base64':base64.b64encode(raw).decode()}
def raw_change(b,name,change):
 rows=[json.loads(x)for x in base64.b64decode(b['files'][name]['base64']).decode().splitlines()];change(rows);update(b,name,(''.join(json.dumps(x)+'\n'for x in rows)).encode())
def target(rows,event):return next(x for x in rows if x['event']==event)
def changed_mask(rows):target(rows,'suicide-query-mask')['mask']^=0x04000000
def missing_row(rows):rows.pop(next(i for i,x in enumerate(rows)if x['event']=='suicide-query-mask'))
def reordered(rows):
 i=next(i for i,x in enumerate(rows)if x['event']=='suicide-query-mask');rows[i],rows[i+1]=rows[i+1],rows[i]
def alias_change(rows):
 r=target(rows,'suicide-query-mask');r['pointer']='0x12345678' # a new unexpected field must also be rejected

def mutate_model(b,change):
 n='retail-captain376-suicide-class-aa.json.gz';model=json.loads(gzip.decompress(base64.b64decode(b['files'][n]['base64'])));change(model);update(b,n,gzip.compress(json.dumps(model).encode(),mtime=0))
def missing_guard(b):
 name='preferred376-complete-union-guards.json';guards=json.loads(base64.b64decode(b['files'][name]['base64']));guards.pop(next(iter(guards)));update(b,name,json.dumps(guards).encode())

cases=[
 ('missing original instruction guard',missing_guard),
 ('changed raw mask',lambda b:raw_change(b,'suicide-class-aa-observe1.jsonl',changed_mask)),
 ('missing raw row',lambda b:raw_change(b,'suicide-class-aa-observe1.jsonl',missing_row)),
 ('raw row order',lambda b:raw_change(b,'suicide-class-aa-observe1.jsonl',reordered)),
 ('unexpected pointer field',lambda b:raw_change(b,'suicide-class-aa-observe1.jsonl',alias_change)),
 ('lost ownership metadata',lambda b:raw_change(b,'suicide-class-aa-observe1.jsonl',lambda r:r[0].update(owned=False))),
 ('wrong runtime lease',lambda b:raw_change(b,'suicide-class-aa-observe1.jsonl',lambda r:r[0].update(remote='127.0.0.1:27048'))),
 ('incomplete footer',lambda b:raw_change(b,'suicide-class-aa-observe1.jsonl',lambda r:r[-1].update(complete=False))),
 ('instrumented control',lambda b:raw_change(b,'suicide-class-aa-control1.jsonl',lambda r:r.insert(1,{'event':'suicide-query-mask','mask':0}))),
 ('changed canonical alias',lambda b:mutate_model(b,lambda m:m['observations'][0]['pointer_aliases'].update({'0x12345678':99}))),
 ('missing canonical ordinal',lambda b:mutate_model(b,lambda m:m['observations'][0]['raw_ordinals'].pop())),
 ('changed classification fact',lambda b:raw_change(b,'suicide-class-aa-observe1.jsonl',lambda r:target(r,'unit-type-mutation')['after'].update(classFlags248=0))),
]

class Captain376Integrity(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.original=json.loads(gzip.decompress(SPEC.read_bytes()))
 def test_complete_relocated_raw_observations(self):
  result=v.verify(SPEC)
  self.assertEqual((result['observations'],result['controls'],result['family_control_comparisons']),(28,12,14))
 def test_original_binary_guard_detects_changed_instruction(self):
  raw=bytearray(1536);struct.pack_into('<I',raw,0x3c,0x80);raw[0x80:0x84]=b'PE\0\0'
  struct.pack_into('<H',raw,0x86,1);struct.pack_into('<H',raw,0x94,0xe0)
  struct.pack_into('<H',raw,0x98,0x10b);struct.pack_into('<I',raw,0xb4,0x6f000000)
  struct.pack_into('<IIII',raw,0x180,512,0x1000,512,0x200);raw[0x200:0x202]=b'\x90\xc3'
  with tempfile.TemporaryDirectory()as td:
   p=Path(td)/'original.dll';p.write_bytes(raw);v.binary_guards(p,{'6f001000':'90c3'})
   with self.assertRaises(AssertionError):v.binary_guards(p,{'6f001000':'90cc'})
 def test_semantic_mutations_rejected_after_rehash(self):
  with tempfile.TemporaryDirectory()as td:
   for label,change in cases:
    with self.subTest(case=label):
     b=copy.deepcopy(self.original);change(b);p=Path(td)/'mutant.json.gz'
     p.write_bytes(gzip.compress(json.dumps(b).encode(),compresslevel=1,mtime=0))
     with self.assertRaises((AssertionError,KeyError,IndexError,ValueError)):v.verify(p)
if __name__=='__main__':unittest.main()
