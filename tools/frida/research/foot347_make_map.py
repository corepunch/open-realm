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
 units=struct.pack('<III',2,0,4)
 for code,radius in [(b'hF3A',31.),(b'hF3G',63.),(b'hF3S',7.),(b'hF3H',95.)]:
  units+=b'hfoo'+code+struct.pack('<I',2)+b'ucol'+struct.pack('<If',2,radius)+code+b'uacq'+struct.pack('<If',2,500.)+code
 abilities=struct.pack('<III',2,0,2)
 for code,target in [(b'AF3G',b'hF3G'),(b'AF3S',b'hF3S')]:
  abilities+=b'Sca1'+code+struct.pack('<I',2)+b'Cha1'+struct.pack('<III',3,1,0)+target+b'\0'+code+b'areq'+struct.pack('<III',3,0,0)+b'\0'+code
 source=cat('war3map.j').decode().replace('\r\n','\n')
 probe=(HERE/'foot347_probe.j').read_text();glob,func=probe.split('endglobals',1)
 source=source.replace('endglobals',glob.removeprefix('globals')+'\nendglobals',1)
 begin=source.index('function S315Mark');end=source.index('function InitGlobals',begin)
 script=source[:begin]+func+'\n'+source[end:]
 overrides={'war3map.j':script.encode(),'war3map.w3u':units,'war3map.w3a':abilities,'war3map.wpm':b'MP3W'+struct.pack('<III',0,64,64)+bytes(4096),r'Scripts\foot347.ai':(HERE/'foot347_probe.ai').read_bytes()}
 with tempfile.TemporaryDirectory(prefix='foot347-') as tmp:
  tmp=Path(tmp);payload=tmp/'payload.mpq';cmd=[tool,'-mpq',str(payload),'pack']
  for i,n in enumerate(dict.fromkeys(members+list(overrides))):
   if n=='(listfile)' or n.endswith(('/',chr(92))):continue
   f=tmp/str(i);f.write_bytes(overrides[n] if n in overrides else cat(n));cmd.extend([str(f),n])
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL);a.output.write_bytes(a.base.read_bytes()[:512]+payload.read_bytes())
 meta=dict(base_sha256=hashlib.sha256(a.base.read_bytes()).hexdigest(),map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path(__file__),HERE/'foot347_probe.j',HERE/'foot347_probe.ai']})
 a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta))
if __name__=='__main__':main()
