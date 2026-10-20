#!/usr/bin/env python3
"""Extend the frozen public payoff120 refill scene; preserve geometry and AI.

Append public markers and a bounded member-axis hold after each timer tick write.
Slow only freshly refilled units to20 to retain shared target owners across a reload.
No original behavioral expectation is rewritten.
"""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
SHA='2a0a30f4bb9b5853c2f58700cceb013f74a3c61783c49eb127d0534051c49a9d'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('new output required')
 raw=a.base.read_bytes()
 if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('frozen public refill map differs')
 probe=Path(__file__).with_name('refresh274_probe.j').read_bytes();tool=str(a.tool.resolve());names=subprocess.check_output([tool,'-mpq',str(a.base),'cat','(listfile)']).decode().splitlines();hashes={}
 with tempfile.TemporaryDirectory(prefix='refresh274-')as tmp:
  root=Path(tmp);cmd=[tool,'-mpq',str(root/'payload.mpq'),'pack']
  for i,name in enumerate(names):
   if name=='(listfile)':continue
   data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
   if name.lower()=='war3map.j':
    marker=b'function PathProbeTick takes nothing returns nothing'
    if data.count(marker)!=1:raise ValueError('tick declaration differs')
    data=data.replace(marker,probe+b'\n'+marker)
    marker=b'    set udg_PathProbeTick = udg_PathProbeTick + 1'
    if data.count(marker)!=1:raise ValueError('tick producer differs')
    data=data.replace(marker,marker+b'\n    call Refresh274Mark()')
    marker=b'            call SetUnitMoveSpeed(udg_PathProbeCrowd[i],100.0)'
    if data.count(marker)!=1:raise ValueError('fresh speed producer differs')
    data=data.replace(marker,b'            call SetUnitMoveSpeed(udg_PathProbeCrowd[i],20.0)')
   hashes[name]=hashlib.sha256(data).hexdigest();f=root/str(i);f.write_bytes(data);cmd += [str(f),name]
  subprocess.run(cmd,check=True);a.output.write_bytes(raw[:512]+(root/'payload.mpq').read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=SHA,sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),member_sha256=hashes,probe_sha256=hashlib.sha256(probe).hexdigest()),indent=2)+'\n')
if __name__=='__main__':main()
