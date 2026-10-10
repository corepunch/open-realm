#!/usr/bin/env python3
"""Copy MAP-02.2's frozen map, replacing only its incorrectly sized shadow mask.

mpqtool pack creates an archive; it must receive every original member.
The terrain, pathing, object data and public probe script remain byte-identical.
"""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('new output required')
 original=a.base.read_bytes()
 assert original[:4]==b'HM3W' and original[512:516]==b'MPQ\x1a'
 tool=str(a.tool.resolve());members=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines();hashes={}
 with tempfile.TemporaryDirectory(prefix='work228-')as tmp:
  root=Path(tmp);cmd=[tool,'-mpq',str(root/'payload.mpq'),'pack']
  for i,name in enumerate(members):
   if name == '(listfile)':continue
   data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
   if name.lower()=='war3map.shd':data=bytes(64*64)
   hashes[name]=hashlib.sha256(data).hexdigest();f=root/str(i);f.write_bytes(data);cmd += [str(f),name]
  subprocess.run(cmd,check=True);a.output.write_bytes(original[:512]+(root/'payload.mpq').read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=hashlib.sha256(original).hexdigest(),sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),member_sha256=hashes),indent=2)+'\n')
if __name__=='__main__':main()
