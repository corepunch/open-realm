#!/usr/bin/env python3
"""Build a fresh public default-Captain/Reset lifetime, preserving existing maps."""
import argparse,hashlib,json,re,subprocess,sys,tempfile
from pathlib import Path
import group032_make_map as flat
HERE=Path(__file__).resolve().parent

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--tool',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--variant',choices=('bare','hall','pair'),default='bare');a=p.parse_args()
 if a.output.exists():p.error('new output required')
 original=a.base.read_bytes();assert original[:4]==b'HM3W'and original[512:516]==b'MPQ\x1a'
 mod=flat.builder();tool=str(a.tool.resolve())
 anchors=[]
 if a.variant=='pair': anchors.extend(["    set udg_R291Mine=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'ngol',-1568.0,-976.0,0.0)",'    call SetUnitX(udg_R291Mine,-1744.0)','    call SetUnitY(udg_R291Mine,-976.0)'])
 if a.variant!='bare': anchors.append("    set udg_R291Hall=CreateUnit(Player(0),'htow',-1936.0,-976.0,0.0)")
 probe=(HERE/'captain291_probe.j').read_text().replace('@ANCHORS@','\n'.join(anchors))
 ai=(HERE/'captain291_probe.ai').read_bytes()
 with tempfile.TemporaryDirectory(prefix='captain291-')as temp:
  root=Path(temp);stage=a.base
  members=[n for n in subprocess.check_output([tool,'-mpq',str(stage),'ls']).decode().splitlines()if n!='(listfile)']
  source=subprocess.check_output([tool,'-mpq',str(a.base),'cat','war3map.j']).decode().replace('\r\n','\n');script=mod.instrument(source,probe,'captain_home');script=re.sub(r'call DefineStartLocation\( ([0-3]), [^)]* \)',r'call DefineStartLocation( \1, -2192, -144 )',script).encode()
  payload=root/'payload.mpq';cmd=[tool,'-mpq',str(payload),'pack'];changed={}
  for i,n in enumerate(members+['Scripts\\captain291.ai']):
   b=script if n=='war3map.j'else ai if n=='Scripts\\captain291.ai'else subprocess.check_output([tool,'-mpq',str(stage),'cat',n]);f=root/str(i);f.write_bytes(b);cmd +=[str(f),n]
   if n in('war3map.j','Scripts\\captain291.ai'):changed[n]=hashlib.sha256(b).hexdigest()
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL);a.output.write_bytes(original[:512]+payload.read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),base_sha256=hashlib.sha256(original).hexdigest(),changed_members=changed,start=[-2192,-144],scenario='captain_home',variant=a.variant,sources={n:hashlib.sha256((HERE/n).read_bytes()).hexdigest()for n in ['captain291_make_map.py','captain291_probe.j','captain291_probe.ai','group032_make_map.py']}),indent=2)+'\n');print(a.output)
if __name__=='__main__':main()
