#!/usr/bin/env python3
"""Keep frozen Work235b geometry/object data and replace only the public probe."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
SHA='cccdb3ac84b2d965363af9c69659010f32fbb4124d94a70695597cf77a1bb162'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('new output required')
 raw=a.base.read_bytes()
 if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('base differs')
 probe=Path(__file__).with_name('work239b_probe.j').read_bytes();tool=str(a.tool.resolve());names=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines();hashes={}
 with tempfile.TemporaryDirectory(prefix='work239-')as tmp:
  root=Path(tmp);cmd=[tool,'-mpq',str(root/'payload.mpq'),'pack']
  for i,name in enumerate(names):
   if name=='(listfile)':continue
   data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
   if name.lower()=='war3map.j':
    setup=[]
    for function in (b'InitCustomPlayerSlots',b'InitCustomTeams',b'InitAllyPriorities',b'config'):
     match=re.search(rb'function '+function+rb' takes nothing returns nothing.*?endfunction',data,re.S)
     if not match:raise ValueError('player config missing')
     setup.append(match.group())
    data=probe+b'\n'+b'\n'.join(setup)+b'\n'
   hashes[name]=hashlib.sha256(data).hexdigest();f=root/str(i);f.write_bytes(data);cmd += [str(f),name]
  subprocess.run(cmd,check=True);a.output.write_bytes(raw[:512]+(root/'payload.mpq').read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=SHA,sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),member_sha256=hashes,probe_sha256=hashlib.sha256(probe).hexdigest()),indent=2)+'\n')
if __name__=='__main__':main()
