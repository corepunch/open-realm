#!/usr/bin/env python3
"""Repack frozen331; replace only main invocation with bounded birth visibility probe."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def u(v):return struct.pack('<I',v)
def obj(old,new,mods):
 b=old.encode()+new.encode()+u(len(mods))
 for field,kind,level,value in mods:
  b+=field.encode()+u(kind)+u(level)+u(0)
  b+=value.encode()+b'\0' if kind==3 else struct.pack('<f',value) if kind in (1,2) else u(value)
  b+=new.encode() if new!='\0'*4 else b'\0'*4
 return b
def split(data):
 pos=4;tables=[]
 for _ in range(2):
  count=struct.unpack_from('<I',data,pos)[0];pos+=4;start=pos
  for i in range(count):
   n=struct.unpack_from('<I',data,pos+8)[0];pos+=12
   for k in range(n):
    typ=struct.unpack_from('<I',data,pos+4)[0];pos+=16
    pos=data.index(0,pos)+1 if typ==3 else pos+4
    pos+=4
  tables.append((count,data[start:pos]))
 assert pos==len(data)
 return tables
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--base',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
 assert not a.output.exists(); tool=str(ROOT/'build/bin/mpqtool');a.output.parent.mkdir(parents=True,exist_ok=True)
 probe=(HERE/'invisibility332_birth_probe_v2.j').read_text();g,body=probe.split('endglobals',1);g=g.removeprefix('globals')
 members=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines();changes={}
 with tempfile.TemporaryDirectory() as t:
  tmp=Path(t);cmd=[tool,'-mpq',str(tmp/'payload'),'pack']
  for i,name in enumerate(members):
   if name=='(listfile)':continue
   data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
   if name=='war3map.j':
    s=data.decode();assert s.count('call I331Init()')==1
    s=s.replace('endglobals',g+'\nendglobals',1).replace('function main takes nothing returns nothing',body+'\nfunction main takes nothing returns nothing').replace('call I331Init()','call I332Init()');data=s.encode()
   f=tmp/str(i);f.write_bytes(data);cmd += [str(f),name]
   if name in ('war3map.j','war3map.w3a'):
    changes[name]=hashlib.sha256(data).hexdigest();a.output.with_suffix('.'+name.split('.')[-1]).write_bytes(data)
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL);a.output.write_bytes(a.base.read_bytes()[:512]+(tmp/'payload').read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=hashlib.sha256(a.base.read_bytes()).hexdigest(),map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),changed=changes,probe_sha256=hashlib.sha256(probe.encode()).hexdigest(),scope='existing299 main/config/geometry preserved; original probe remains dormant'),indent=2)+'\n')
if __name__=='__main__':main()
