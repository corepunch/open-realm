#!/usr/bin/env python3
"""Original complete169680: first-member prediction, thresholds and counted visits.

Uses original allocator/owner setup from the spatial rig. Fixture group/member/path
storage and clocks are supplied explicitly; no target visibility or AI logic is
invented. Only inherited Storm allocation adapters replace executable functions.
"""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_rig import Rig

def original(binary):
 r=Rig(binary,64,64);e=r.e
 e.w(0x6fd541b8,0x3ea8f5c3) # Initialized0.33, verified by the live capture.
 group=e.fixture(0x100);member=e.fixture(0x2c);path=e.fixture(0xc0)
 e.call(0x6f14ee90,r.xy,1,edx=0);mover=e.r(r.xy)
 e.w(group+0x28,member);e.w(group+0x38,1);e.w(group+0x3c,path);e.w(member+0x14,mover)
 e.uc.mem_write(r.owner+0x54,struct.pack('<fIf',8,0,300))
 e.uc.mem_write(mover+0x70,struct.pack('<fI',8,0))
 e.uc.mem_write(mover+0x78,struct.pack('<4f',0,0,0,0))
 def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]
 def value(x):return struct.unpack('<f',struct.pack('<I',x))[0]
 points=[(bits(i/4),0)for i in range(2001)]
 for x in(16/.33,16.5/.33,17.5/.33,131.5/.33,132/.33,132.5/.33):
  for delta in range(-16,17):points.append((bits(x)+delta,0))
 points.extend((bits(x),bits(y))for x,y in((0,0),(3,4),(-3,4),(25,25),(36,36),(280,280),(284,284),(512,512),(4096,4096),(-4096,4096)))
 rows=[]
 for x,y in points:
  e.w(path+0x1c,x,y)
  for flags in(0,0x400):
   e.w(group+0x80,flags);e.w(group+0x64,0xffffffff)
   e.call(0x6f169680,group)
   reload=e.r(group+0x64);assert 16<=reload<=297
   rows.append([x,y,flags,reload])
 prediction=[]
 for dt in(-.03125,0,.00001,.03125,.09,1):
  for vx,vy in((0,0),(8,-6),(-6,8)):
   now=bits(8+dt)
   e.w(r.owner+0x54,now,0,bits(300))
   e.w(mover+0x70,bits(8),0)
   e.w(mover+0x78,bits(16),bits(20),bits(vx),bits(vy))
   for gx,gy in((16,20),(66,20),(16,70),(416,20),(299,303)):
    e.w(path+0x1c,bits(gx),bits(gy))
    for flags in(0,0x400):
     e.w(group+0x80,flags);e.w(group+0x64,0xffffffff);e.call(0x6f169680,group)
     prediction.append([bits(16),bits(20),bits(vx),bits(vy),now,bits(gx),bits(gy),flags,e.r(group+0x64)])
 countdown=[]
 for initial in range(298):
  e.w(group+0x64,initial);e.call(0x6f169680,group)
  countdown.append([initial,e.r(group+0x64)])
 return dict(rows=rows,prediction=prediction,countdown=countdown,scope=__doc__)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 result=original(a.binary);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:len(v)for k,v in result.items()if k!='scope'}))
if __name__=='__main__':main()
