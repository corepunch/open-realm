#!/usr/bin/env python3
"""Replace only the script in the fixed, shadow-corrected reacquisition arena."""
import argparse,hashlib,json,re,subprocess,tempfile
from pathlib import Path
SHA='841f9e9cc783c34d8aaadfabe6d6ba3eac7384d621be05044380a005cdebbee2'
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('base','tool','probe','output'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args()
 if a.output.exists():p.error('new output required')
 raw=a.base.read_bytes()
 if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('source arena differs')
 tool=str(a.tool.resolve());names=subprocess.check_output([tool,'-mpq',str(a.base),'ls']).decode().splitlines();hashes={}
 with tempfile.TemporaryDirectory(prefix='target253-') as tmp:
  root=Path(tmp);cmd=[tool,'-mpq',str(root/'payload.mpq'),'pack']
  for i,name in enumerate(names):
   if name=='(listfile)':continue
   data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
   if name.lower()=='war3map.j':
    script=data.decode();probe=a.probe.read_text()
    block=re.search(r'^globals\n(.*?)^endglobals\n',probe,re.M|re.S)
    if block is None or script.count('\nendglobals')!=1 or script.count('call PathProbeInit()')!=1:raise ValueError('script injection boundary differs')
    script=script.replace('\nendglobals','\n'+block.group(1)+'endglobals',1)
    script=script.replace('\nendglobals','\nendglobals\n'+probe[block.end():],1)
    data=script.replace('call PathProbeInit()','call F253Init()',1).encode()
   hashes[name]=hashlib.sha256(data).hexdigest();f=root/str(i);f.write_bytes(data);cmd.extend([str(f),name])
  subprocess.run(cmd,check=True);a.output.write_bytes(raw[:512]+(root/'payload.mpq').read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(base_sha256=SHA,sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),member_sha256=hashes),indent=2)+'\n')
if __name__=='__main__':main()
