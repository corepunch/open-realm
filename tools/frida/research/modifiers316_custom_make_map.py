#!/usr/bin/env python3
"""Author non-stock Slow/Bloodlust percentage fields in a finite probe map."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();assert not a.output.exists()
tool=str(ROOT/'build/bin/mpqtool');members=subprocess.check_output([tool,'-mpq',str(a.base),'ls'],text=True).splitlines()
ability=struct.pack('<II',2,2)
for code,field,value in [(b'Aslo',b'Slo1',.37),(b'ACbl',b'Blo2',.17)]:
 ability+=code+bytes(4)+struct.pack('<I',1)+field+struct.pack('<IIIfI',2,1,1 if field==b'Slo1'else 2,value,0)
ability+=struct.pack('<I',0)
with tempfile.TemporaryDirectory(prefix='modifiers316-custom-')as d:
 temp=Path(d);payload=temp/'payload.mpq';cmd=[tool,'-mpq',str(payload),'pack']
 if 'war3map.w3a'not in members:members.append('war3map.w3a')
 for i,name in enumerate(members):
  if name=='(listfile)':continue
  data=ability if name=='war3map.w3a'else subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
  if name=='war3map.w3i':
   data=bytearray(data);pos=12
   assert struct.unpack_from('<I',data)[0]in (18,25)
   for _ in range(4):pos=data.index(0,pos)+1
   pos+=56;struct.pack_into('<I',data,pos,struct.unpack_from('<I',data,pos)[0]|0x100);data=bytes(data)
  f=temp/str(i);f.write_bytes(data);cmd.extend([str(f),name])
 subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL)
 header=bytearray(a.base.read_bytes()[:512]);pos=header.index(0,8)+1
 struct.pack_into('<I',header,pos,struct.unpack_from('<I',header,pos)[0]|0x100)
 a.output.write_bytes(bytes(header)+payload.read_bytes())
a.output.with_suffix('.json').write_text(json.dumps({'task':'NUM-03.2','variant':'Slow .37 Bloodlust .17','base_sha256':hashlib.sha256(a.base.read_bytes()).hexdigest(),'map_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'ability_sha256':hashlib.sha256(ability).hexdigest(),'builder_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},indent=2)+'\n')
