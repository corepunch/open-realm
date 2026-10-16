#!/usr/bin/env python3
"""Expand frozen Work228c to a flat4096-world-square with an authored flight Chaos alias."""
import argparse,hashlib,json,subprocess,tempfile,re,struct
from pathlib import Path
SHA='5ef45d6b906c270734b26931772194ea3306676f68714e078c12eb1841785e06'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('new output required')
 original=a.base.read_bytes()
 if hashlib.sha256(original).hexdigest()!=SHA:raise ValueError('base differs')
 probe=Path(__file__).with_name('work235_probe.j').read_bytes();tool=str(a.tool.resolve());members=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines();hashes={}
 with tempfile.TemporaryDirectory(prefix='work235-')as tmp:
  root=Path(tmp);cmd=[tool,'-mpq',str(root/'payload.mpq'),'pack']
  for i,name in enumerate(members):
   if name=='(listfile)':continue
   data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
   if name.lower()=='war3map.w3e':
    offset=13
    for k in range(2):offset+=4+4*struct.unpack_from('<I',data,offset)[0]
    vertex=data[offset+16:offset+23]
    data=data[:offset]+struct.pack('<IIff',33,33,0,0)+vertex*(33*33)
   elif name.lower()=='war3map.wpm':data=data[:8]+struct.pack('<II',128,128)+bytes(128*128)
   elif name.lower()=='war3map.shd':data=bytes(128*128)
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
  ability=struct.pack('<III',2,0,1)+b'Sca1AS33'+struct.pack('<I',2)+b'Cha1'+struct.pack('<III',3,1,0)+b'ehip\0AS33'+b'areq'+struct.pack('<III',3,0,0)+b'\0AS33'
  if 'war3map.w3a' in hashes:raise ValueError('ability member exists')
  f=root/'ability';f.write_bytes(ability);cmd += [str(f),'war3map.w3a'];hashes['war3map.w3a']=hashlib.sha256(ability).hexdigest()
  subprocess.run(cmd,check=True);a.output.write_bytes(original[:512]+(root/'payload.mpq').read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=SHA,sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),member_sha256=hashes,probe_sha256=hashlib.sha256(probe).hexdigest()),indent=2)+'\n')
if __name__=='__main__':main()
