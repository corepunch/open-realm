#!/usr/bin/env python3
"""Replace the probe in a pinned accepted flat arena; preserve all other members."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('fresh output required')
 probe=Path(__file__).with_name('patrol276_probe.j').read_text()
 tool=str(a.tool.resolve());raw=a.base.read_bytes();hashes={}
 with tempfile.TemporaryDirectory(prefix='patrol276-')as tmp:
  root=Path(tmp);cmd=[tool,'-mpq',str(root/'payload.mpq'),'pack']
  names=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines()
  for i,name in enumerate(names):
   if name=='(listfile)':continue
   data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
   if name=='war3map.j':
    source=data.decode().replace('\r\n','\n')
    source,n=re.subn(r' unit udg_R264Actor=null\n.*?endfunction\n\n(?=\nfunction InitGlobals)',probe.removeprefix('globals\n')+'\n',source,flags=re.S)
    if n!=1:raise ValueError('accepted arena probe boundary differs')
    data=source.encode()
   hashes[name]=hashlib.sha256(data).hexdigest();f=root/str(i);f.write_bytes(data);cmd += [str(f),name]
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL);a.output.write_bytes(raw[:512]+(root/'payload.mpq').read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=hashlib.sha256(raw).hexdigest(),map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),members=hashes,probe_sha256=hashlib.sha256(probe.encode()).hexdigest()),indent=2)+'\n')
if __name__=='__main__':main()
