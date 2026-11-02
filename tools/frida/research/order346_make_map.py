#!/usr/bin/env python3
"""Replace the existing flat probe arena's script and AI-enabled blocked goal probe."""
import argparse, hashlib, json, subprocess, tempfile, struct
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--base',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True)
 p.add_argument('--footman-cargo-size',type=int)
 p.add_argument('--naval-board-arena',action='store_true')
 a=p.parse_args()
 if a.output.exists():p.error('output must be new')
 tool=str(ROOT/'build/bin/mpqtool')
 def cat(n):return subprocess.check_output([tool,'-mpq',str(a.base),'cat',n])
 members=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines()
 probe=(HERE/'order346_probe.j').read_text()
 # Standalone script: the supplied arena already owns terrain/player metadata.
 script=probe+'\nfunction config takes nothing returns nothing\n call SetPlayers(2)\n call SetTeams(2)\n call SetPlayerController(Player(0),MAP_CONTROL_USER)\n call SetPlayerSlotAvailable(Player(0),MAP_CONTROL_USER)\n call SetPlayerController(Player(1),MAP_CONTROL_COMPUTER)\n call SetPlayerSlotAvailable(Player(1),MAP_CONTROL_COMPUTER)\nendfunction\nfunction main takes nothing returns nothing\n call InitBlizzard()\n call PathProbeInit()\nendfunction\n'
 overrides={'war3map.j':script.encode(),'order346.ai':b'function main takes nothing returns nothing\n loop\n call Sleep(100.0)\n endloop\nendfunction\n'}
 if a.naval_board_arena:
  data=bytearray(cat('war3map.wpm'))
  assert data[:4]==b'MP3W' and struct.unpack_from('<II',data,8)==(64,64)
  for y in range(64):
   for x in range(64):data[16+y*64+x]=2 if x<32 else 64
  overrides['war3map.wpm']=bytes(data)
 if a.footman_cargo_size is not None:
  data=cat('war3map.w3u')
  assert struct.unpack_from('<II',data)==(2,0), 'expected no original unit overrides'
  original=b'hfoo'+bytes(4)+struct.pack('<I',1)+b'ucar'+struct.pack('<Ii',0,a.footman_cargo_size)+bytes(4)
  overrides['war3map.w3u']=struct.pack('<II',2,1)+original+data[8:]

 with tempfile.TemporaryDirectory(prefix='order346-') as tmp:
  tmp=Path(tmp);payload=tmp/'packed.mpq';cmd=[tool,'-mpq',str(payload),'pack']
  for i,n in enumerate(dict.fromkeys(members+list(overrides))):
   if n=='(listfile)':continue
   data=overrides[n] if n in overrides else cat(n)
   f=tmp/str(i);f.write_bytes(data);cmd.extend([str(f),n])
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL)
  a.output.write_bytes(a.base.read_bytes()[:512]+payload.read_bytes())
 meta={'base':str(a.base),'base_sha256':hashlib.sha256(a.base.read_bytes()).hexdigest(),'map_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'sources':{str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [Path(__file__),HERE/'order346_probe.j']},'overrides':{n:hashlib.sha256(d).hexdigest() for n,d in overrides.items()}}
 a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n')
 print(json.dumps(meta))
if __name__=='__main__':main()
