#!/usr/bin/env python3
"""Extend the frozen Captain320 map with a public Slow-on/off command pair."""
import argparse,hashlib,json,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('new output required')
 raw=a.base.read_bytes();expected='c332b33e6f97c5e5356e63a723aea5aea853def914af3686d8f2925ccac64af1'
 if hashlib.sha256(raw).hexdigest()!=expected:p.error('Captain320 base differs')
 tool=str(ROOT/'build/bin/mpqtool')
 with tempfile.TemporaryDirectory(prefix='cast321-')as tmp:
  tmp=Path(tmp);packed=tmp/'packed.mpq';command=[tool,'-mpq',str(packed),'pack'];changed={}
  for i,name in enumerate(subprocess.check_output([tool,'-mpq',str(a.base),'cat','(listfile)']).decode().splitlines()):
   if name=='(listfile)':continue
   data=subprocess.check_output([tool,'-mpq',str(a.base),'cat',name])
   if name=='war3map.j':
    old=b'call IssueImmediateOrder(udg_C320Caster,"slowoff")';assert data.count(old)==1
    data=data.replace(old,b'call IssueImmediateOrder(udg_C320Caster,"slowon")\n  '+old);changed[name]=hashlib.sha256(data).hexdigest()
   f=tmp/str(i);f.write_bytes(data);command.extend([str(f),name])
  subprocess.run(command,check=True,stdout=subprocess.DEVNULL);a.output.write_bytes(raw[:512]+packed.read_bytes())
 a.output.with_suffix('.json').write_text(json.dumps(dict(task='ORDER-01.13',base_sha256=expected,map_sha256=hashlib.sha256(a.output.read_bytes()).hexdigest(),builder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),changed_members=changed),indent=2)+'\n')
if __name__=='__main__':main()
