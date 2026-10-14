#!/usr/bin/env python3
"""Public ability separation lifecycle probe; stock morphs and authored Avatar/Channel."""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
from sep_research_map import w3a_channel
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('new output required')
 tool=str(ROOT/'build/bin/mpqtool')
 with tempfile.TemporaryDirectory(prefix='sep323-') as td:
  td=Path(td);raw=td/'raw.w3m'
  subprocess.run([sys.executable,str(HERE/'group032_make_map.py'),'--base',str(a.base),'--probe',str(HERE/'avatar323_probe.j'),'--preload-output','separation323.txt','--task','SEP-01.2','--wpm-blocked','[]','--output',str(raw)],check=True)
  ability=w3a_channel();ability=ability[:8]+struct.pack('<I',4)+ability[12:]
  mods=[('aher',0,0,0),('amcs',0,1,0),('acdn',2,1,0.0),('adur',2,1,2.0),('ahdu',2,1,2.0)]
  extra=w3a_channel()[12:].replace(b'ASch',b'ASc1')
  old=b'Ncl5'+struct.pack('<IIII',0,1,5,0);new=b'Ncl5'+struct.pack('<IIII',0,1,5,1)
  assert extra.count(old)==1;ability+=extra.replace(old,new)
  ability+=b'AHavA323'+struct.pack('<I',len(mods))
  for f,k,l,v in mods:ability+=f.encode()+struct.pack('<III',k,l,0)+struct.pack('<i' if k==0 else '<f',v)+b'A323'
  extra=b'AHavA324'+struct.pack('<I',len(mods))
  for f,k,l,v in mods:
   if f in ('adur','ahdu'):v=0.4
   extra+=f.encode()+struct.pack('<III',k,l,0)+struct.pack('<i' if k==0 else '<f',v)+b'A324'
  ability+=extra
  payload=td/'packed.mpq';cmd=[tool,'-mpq',str(payload),'pack'];changed={}
  names=subprocess.check_output([tool,'-mpq',str(raw),'cat','(listfile)']).decode().splitlines()
  if 'war3map.w3a' not in names:names.append('war3map.w3a')
  names.append(r'Scripts\separation323.ai')
  for i,n in enumerate(names):
   if n=='(listfile)':continue
   data=(HERE/'separation323_probe.ai').read_bytes() if n==r'Scripts\separation323.ai' else ability if n=='war3map.w3a' else subprocess.check_output([tool,'-mpq',str(raw),'cat',n])
   if n=='war3map.shd':data=bytes(4096)
   f=td/str(i);f.write_bytes(data);cmd.extend([str(f),n]);changed[n]=hashlib.sha256(data).hexdigest()
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL)
  a.output.write_bytes(raw.read_bytes()[:512]+payload.read_bytes())
  a.output.with_suffix('.json').write_text(json.dumps(dict(task='SEP-01.2',base_sha256=hashlib.sha256(a.base.read_bytes()).hexdigest(),map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),members=changed),indent=2)+'\n')
if __name__=='__main__':main()
