#!/usr/bin/env python3
"""Channel interruption probe with the unchanged separation323 ability/unit objects."""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--base',type=Path,required=True)
 p.add_argument('--objects',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True)
 p.add_argument('--preload-output',default='channel325.txt')
 p.add_argument('--cross-channel',action='store_true')
 p.add_argument('--probe',type=Path,default=HERE/'channel325_probe.j')
 p.add_argument('--pause-end',type=int,choices=(20,40),default=20)
 a=p.parse_args()
 if a.output.exists():p.error('output must be new')
 if sha(a.objects)!='bb9a45c7ed3dc5ac5920ea1f6f71b6bcc61d9c5956bc07736455ddc1dadaeda4':p.error('expected frozen Separation323g objects')
 tool=str(ROOT/'build/bin/mpqtool')
 with tempfile.TemporaryDirectory(prefix='channel325-') as td:
  td=Path(td);raw=td/'raw.w3m'
  subprocess.run([sys.executable,str(HERE/'group032_make_map.py'),'--base',str(a.base),'--probe',str(a.probe),'--preload-output',a.preload_output,'--task','SEP-01.2','--wpm-blocked','[]','--replace',f'PAUSE_END={a.pause_end}','--replace',f'CROSS_CHANNEL={str(a.cross_channel).lower()}','--replace',f'RECAST_ORDER={"thunderclap" if a.cross_channel else "channel"}','--output',str(raw)],check=True)
  names=subprocess.check_output([tool,'-mpq',str(raw),'cat','(listfile)']).decode().splitlines()
  if 'war3map.w3a' not in names:names.append('war3map.w3a')
  payload=td/'packed.mpq';cmd=[tool,'-mpq',str(payload),'pack'];members={}
  for i,n in enumerate(names):
   if n=='(listfile)':continue
   source=a.objects if n in ('war3map.w3a','war3map.w3u') else raw
   data=subprocess.check_output([tool,'-mpq',str(source),'cat',n])
   if n=='war3map.w3a' and a.cross_channel:
    from sep_research_map import w3a_channel
    extra=w3a_channel()[12:].replace(b'ASch',b'ASc2').replace(b'channel\0',b'thunderclap\0')
    old=b'Ncl5'+struct.pack('<IIII',0,1,5,0);new=b'Ncl5'+struct.pack('<IIII',0,1,5,1)
    assert extra.count(old)==1
    data=data[:8]+struct.pack('<I',4)+data[12:]+extra.replace(old,new)
   if n=='war3map.shd':data=bytes(4096)
   f=td/str(i);f.write_bytes(data);cmd.extend([str(f),n]);members[n]=sha(f)
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL)
  a.output.write_bytes(raw.read_bytes()[:512]+payload.read_bytes())
  a.output.with_suffix('.json').write_text(json.dumps(dict(task='SEP-01.2/channel325',pause_end=a.pause_end,cross_channel=a.cross_channel,base_sha256=sha(a.base),objects_sha256=sha(a.objects),map_sha256=sha(a.output),members=members,probe_sha256=sha(a.probe),sources={n:sha(HERE/n) for n in ['channel325_make_map.py','channel325_probe.j','group032_make_map.py','sep_research_map.py']}),indent=2)+'\n')
if __name__=='__main__':main()
