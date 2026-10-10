#!/usr/bin/env python3
"""Execute original UI point-candidate attachment with native canonical factories."""
import argparse,itertools,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_rig import Rig
def original(binary):
 rows=[]
 for bits,special,forced,alt,present in itertools.product((0,1,2,4,8,16,32,64),(0,1),(-1,0,1),(0,1),(1,3,7)):
  r=Rig(binary);e=r.e;e.call(0x6f14ee90,r.xy,1,edx=0);mover=e.r(r.xy)
  unit=e.fixture(0x300);e.w(unit+0x16c,*e.r(mover+0x14,2));e.w(unit+0x1fc,bits,forced);e.w(unit+0x5c,0x20000000 if special else 0)
  ctx=e.fixture(28);requests=[]
  for i in range(3):
   wrapper=e.fixture(16);e.call(0x6f21e600,wrapper);e.w(wrapper+4,1);e.call(0x6f89c890,wrapper)
   requests.append(e.call(0x6f054530,e.r(wrapper+8),edx=e.r(wrapper+12)))
   e.w(ctx+4*i,wrapper if present&(1<<i)else 0)
  e.w(ctx+24,alt);result=e.call(0x6f6b8c10,unit,edx=ctx)
  if e.esp_after!=4 or result!=1:raise ValueError('attachment ABI differs')
  attached=[]
  for i,request in enumerate(requests):
   if e.r(request+0x1c,2)==e.r(mover+0x14,2):attached.append(i)
   if e.r(request+0xac)!=0xffffffff:raise ValueError('attachment unexpectedly ready')
  rows.append(dict(bits=bits,special=special,forced=forced,alt=alt,present=present,attached=attached))
 return dict(rows=rows)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 s=original(a.binary);a.report.write_text(json.dumps(s,indent=1)+'\n');print(json.dumps(dict(rows=len(s['rows']))))
if __name__=='__main__':main()
