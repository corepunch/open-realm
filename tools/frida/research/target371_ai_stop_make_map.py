#!/usr/bin/env python3
"""Build idle computer Stop/AI task-dispatch probe; no engine writes."""
import argparse, hashlib, json, subprocess, tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--base',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True)
 a=p.parse_args()
 if a.output.exists():p.error('output must be new')
 tool=str(ROOT/'build/bin/mpqtool')
 def cat(n):return subprocess.check_output([tool,'-mpq',str(a.base),'cat',n])
 members=[n for n in cat('(listfile)').decode().splitlines()if not n.endswith('/')]
 probe=(HERE/'target371_ai_stop_probe.j').read_text()
 # Standalone script: the supplied arena already owns terrain/player metadata.
 script=probe+'\nfunction config takes nothing returns nothing\n call SetPlayers(2)\n call SetTeams(2)\n call SetPlayerController(Player(0),MAP_CONTROL_USER)\n call SetPlayerSlotAvailable(Player(0),MAP_CONTROL_USER)\n call SetPlayerController(Player(1),MAP_CONTROL_COMPUTER)\n call SetPlayerSlotAvailable(Player(1),MAP_CONTROL_COMPUTER)\nendfunction\nfunction main takes nothing returns nothing\n call InitBlizzard()\n call PathProbeInit()\nendfunction\n'
 overrides={'war3map.j':script.encode(),'target371_ai_stop.ai':b'function main takes nothing returns nothing\n loop\n call Sleep(100.0)\n endloop\nendfunction\n'}
 with tempfile.TemporaryDirectory(prefix='target371_ai_stop-') as tmp:
  tmp=Path(tmp);payload=tmp/'packed.mpq';cmd=[tool,'-mpq',str(payload),'pack']
  for i,n in enumerate(dict.fromkeys(members+list(overrides))):
   if n=='(listfile)':continue
   data=overrides[n] if n in overrides else cat(n)
   f=tmp/str(i);f.write_bytes(data);cmd.extend([str(f),n])
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL)
  a.output.write_bytes(a.base.read_bytes()[:512]+payload.read_bytes())
 meta={'base':str(a.base),'base_sha256':hashlib.sha256(a.base.read_bytes()).hexdigest(),'map_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'sources':{str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [Path(__file__),HERE/'target371_ai_stop_probe.j']},'overrides':{n:hashlib.sha256(d).hexdigest() for n,d in overrides.items()}}
 a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n')
 print(json.dumps(meta))
if __name__=='__main__':main()
