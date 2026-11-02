#!/usr/bin/env python3
"""Build a public Chaos/shared-radius lifetime probe from the flat arena."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('fresh output required')
 tool=str(ROOT/'build/bin/mpqtool')
 def cat(n):return subprocess.check_output([tool,'-mpq',str(a.base),'cat',n])
 members=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines()
 units=struct.pack('<III',2,0,1);code=b'hP49'
 units+=b'hfoo'+code+struct.pack('<I',9)
 for key,value in [(b'umvs',333),(b'umis',1),(b'umas',522),(b'uhpm',5000),(b'umpm',1000),(b'umpi',1000)]:units+=key+struct.pack('<Ii',0,value)+code
 for key,value in [(b'ucol',8.),(b'umvr',.37),(b'uprw',45.)]:units+=key+struct.pack('<If',2,value)+code
 abilities=struct.pack('<III',2,0,0)
 source=cat('war3map.j').decode().replace('\r\n','\n')
 probe=(HERE/'power349_probe.j').read_text();glob,func=probe.split('endglobals',1)
 source=source.replace('endglobals',glob.removeprefix('globals')+'\nendglobals',1)
 begin=source.index('function S315Mark');end=source.index('function InitGlobals',begin)
 script=source[:begin]+func+'\n'+source[end:]
 overrides={'war3map.j':script.encode(),'war3map.w3u':units,'war3map.w3a':abilities,'war3map.wpm':b'MP3W'+struct.pack('<III',0,64,64)+bytes(4096)}
 with tempfile.TemporaryDirectory(prefix='power349-') as tmp:
  tmp=Path(tmp);payload=tmp/'payload.mpq';cmd=[tool,'-mpq',str(payload),'pack']
  for i,n in enumerate(dict.fromkeys(members+list(overrides))):
   if n=='(listfile)' or n.endswith(('/',chr(92))):continue
   f=tmp/str(i);f.write_bytes(overrides[n] if n in overrides else cat(n));cmd.extend([str(f),n])
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL);a.output.write_bytes(a.base.read_bytes()[:512]+payload.read_bytes())
 meta=dict(base_sha256=hashlib.sha256(a.base.read_bytes()).hexdigest(),map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__),HERE/'power349_probe.j']})
 a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta))
if __name__=='__main__':main()
