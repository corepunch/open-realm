#!/usr/bin/env python3
"""Replay the reviewed Captain376 bounded raw observations without rewriting inputs."""
from pathlib import Path
import argparse,ast,base64,gzip,hashlib,json,tempfile,struct

def binary_guards(binary,entries):
 raw=binary.read_bytes();pe=struct.unpack_from('<I',raw,0x3c)[0];assert raw[pe:pe+4]==b'PE\0\0'
 count=struct.unpack_from('<H',raw,pe+6)[0];optsize=struct.unpack_from('<H',raw,pe+20)[0];opt=pe+24
 assert struct.unpack_from('<H',raw,opt)[0]==0x10b
 base=struct.unpack_from('<I',raw,opt+28)[0];sections=[]
 for i in range(count):
  at=opt+optsize+i*40;vsize,rva,size,offset=struct.unpack_from('<IIII',raw,at+8);sections.append((rva,size,offset))
 for address,value in entries.items():
  rva=int(address,16)-base;want=bytes.fromhex(value)
  section=next((s for s in sections if s[0]<=rva and rva+len(want)<=s[0]+s[1]),None);assert section,address
  offset=section[2]+rva-section[0];assert raw[offset:offset+len(want)]==want,address

def verify(path,binary=None):
 b=json.loads(gzip.decompress(path.read_bytes()));assert b['version']==1
 if binary is not None:assert hashlib.sha256(binary.read_bytes()).hexdigest()==b['binary_sha256']
 with tempfile.TemporaryDirectory(prefix='captain376-native-')as root:
  root=Path(root)
  for name,entry in b['files'].items():
   assert not Path(name).is_absolute()and'..'not in Path(name).parts
   raw=base64.b64decode(entry['base64'],validate=True);assert hashlib.sha256(raw).hexdigest()==entry['sha256'],name
   f=root/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(raw)
  instructions=json.loads((root/'preferred376-complete-union-guards.json').read_text());data_guards=json.loads((root/'preferred376-complete-data-guards.json').read_text())
  assert len(instructions)==7801 and len(data_guards)==16
  assert sum(len(bytes.fromhex(value))for value in data_guards.values())==298
  if binary is not None:
   binary_guards(binary,instructions);binary_guards(binary,data_guards)
  capture_modes=[]
  included_hashes={entry['sha256']for entry in b['files'].values()}
  admin={'metadata','owned-loading-input','loading-key','preload-file'}
  for name in b['files']:
   if not name.endswith('.jsonl'):continue
   raw=[json.loads(x)for x in (root/name).read_text().splitlines()];meta=raw[0]
   assert meta['event']=='metadata'and meta['owned']is True and meta['sha256']==b['binary_sha256'],name
   assert (meta['display'],meta['remote'])in[(':98','127.0.0.1:27049'),(':99','127.0.0.1:27050')],name
   assert meta['mode']in['observe','control'];capture_modes.append(meta['mode'])
   assert all(h in included_hashes for h in meta['source_sha256'].values()),name
   assert raw[-1]['event']=='preload-file'and raw[-1]['complete']is True,name
   if meta['mode']=='control':assert all(x['event']in admin for x in raw),name
   else:assert any(x['event']=='marker'for x in raw),name
  assert capture_modes.count('observe')==28 and capture_modes.count('control')==12
  results=[]
  for family in b['families']:
   model=json.loads(gzip.decompress((root/family['model']).read_bytes()));assert model['binary_sha256']==b['binary_sha256']
   script=root/family['freezer'];tree=ast.parse(script.read_text());prefix=[]
   for node in tree.body:
    if isinstance(node,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='paths'for t in node.targets):break
    prefix.append(node)
   else:raise AssertionError('missing read-only normalizer boundary')
   ns={'__file__':str(script),'__name__':'captain376_bounded_replay'}
   exec(compile(ast.Module(body=prefix,type_ignores=[]),str(script),'exec',optimize=0),ns)
   rows=ns.get('rows',ns.get('ar',ns['a'].get('records')));assert rows==model['records'],family['name']
   assert ns['a']['public']==model['markers'],family['name']
   # Verify each original pointer map, row ordinal, and void EAX relation retained by the bundle.
   for observed,expected in zip([ns['a'],ns['b']],model.get('observations',[])):
    for key,value in expected.items():
     assert key in observed and observed[key]==value,(family['name'],key)
   results.append({'name':family['name'],'records':len(rows),'public_markers':len(model['markers'])})
 return {'passed':True,'binary_sha256':b['binary_sha256'],'families':results,'observations':2*len(results),'controls':12,'family_control_comparisons':len(results),'files':len(b['files']),'instructions':len(instructions),'data_guards':len(data_guards)}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('bundle',type=Path);p.add_argument('--report',type=Path);p.add_argument('--binary',type=Path,required=True);a=p.parse_args();r=verify(a.bundle,a.binary)
 if a.report:a.report.write_text(json.dumps(r,indent=2)+'\n')
 print(json.dumps(r,indent=2))
