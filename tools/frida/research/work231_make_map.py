#!/usr/bin/env python3
"""Retain every frozen Work228c member except the public point-query script."""
import argparse,hashlib,json,subprocess,tempfile,re
from pathlib import Path
SHA='5ef45d6b906c270734b26931772194ea3306676f68714e078c12eb1841785e06'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('new output required')
 original=a.base.read_bytes()
 if hashlib.sha256(original).hexdigest()!=SHA:raise ValueError('base differs')
 probe=Path(__file__).with_name('work231_probe.j').read_bytes();tool=str(a.tool.resolve());members=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines();hashes={}
 with tempfile.TemporaryDirectory(prefix='work231-')as tmp:
  root=Path(tmp);cmd=[tool,'-mpq',str(root/'payload.mpq'),'pack']
  for i,name in enumerate(members):
   if name=='(listfile)':continue
   data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
   if name.lower()=='war3map.j':
    config=re.search(rb'function config takes nothing returns nothing.*?endfunction',data,re.S)
    if not config:raise ValueError('missing config')
    setup=[]
    for function in (b'InitCustomPlayerSlots',b'InitCustomTeams',b'InitAllyPriorities'):
     match=re.search(rb'function '+function+rb' takes nothing returns nothing.*?endfunction',data,re.S)
     if not match:raise ValueError('missing player setup')
     setup.append(match.group())
    data=probe+b'\n'+b'\n'.join(setup)+b'\n'+config.group()+b'\n'
   hashes[name]=hashlib.sha256(data).hexdigest();f=root/str(i);f.write_bytes(data);cmd += [str(f),name]
  subprocess.run(cmd,check=True);a.output.write_bytes(original[:512]+(root/'payload.mpq').read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=SHA,sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),member_sha256=hashes,probe_sha256=hashlib.sha256(probe).hexdigest()),indent=2)+'\n')
if __name__=='__main__':main()
