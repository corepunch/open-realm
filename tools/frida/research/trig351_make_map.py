#!/usr/bin/env python3
"""Replace only the public trig probe in the frozen Power307 control arena."""
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('fresh output required')
 tool=str(ROOT/'build/bin/mpqtool')
 def cat(name):return subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
 source=cat('war3map.j').decode().replace('\r\n','\n')
 old=(HERE/'power307_probe.j').read_text().split('endglobals',1)[0].removeprefix('globals')
 if old not in source:raise ValueError('frozen control probe globals absent')
 probe=(HERE/'trig351_probe.j').read_text();glob,func=probe.split('endglobals',1)
 source=source.replace(old,glob.removeprefix('globals'),1)
 begin=source.index('function P307Mark');end=source.index('function InitGlobals',begin)
 script=(source[:begin]+func+'\n'+source[end:]).encode()
 names=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines()
 with tempfile.TemporaryDirectory(prefix='trig351-')as tmp:
  tmp=Path(tmp);payload=tmp/'map.mpq';cmd=[tool,'-mpq',str(payload),'pack']
  for i,name in enumerate(names):
   if name=='(listfile)'or name.endswith(('/',chr(92))):continue
   f=tmp/str(i);f.write_bytes(script if name=='war3map.j'else cat(name));cmd.extend([str(f),name])
  subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL);a.output.write_bytes(a.base.read_bytes()[:512]+payload.read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=hashlib.sha256(a.base.read_bytes()).hexdigest(),map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),probe_sha256=hashlib.sha256((HERE/'trig351_probe.j').read_bytes()).hexdigest()),indent=2)+'\n')
if __name__=='__main__':main()
