#!/usr/bin/env python3
"""Original walkable-mesh ray/triangle kernel; no rewritten arithmetic or callees."""
import argparse,hashlib,json,math,random,struct
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
F=lambda x:struct.unpack('<I',struct.pack('<f',x))[0]
def original(binary):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
 from unicorn import x86_const as X
 raw=Path(binary).read_bytes()
 if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('wrong game.dll')
 pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24
 base,size=[struct.unpack_from('<I',raw,opt+n)[0]for n in (28,56)]
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  s=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',raw,s+12)
  if n:u.mem_write(base+va,raw[off:off+n])
 u.mem_map(0x10000000,0x100000)
 stop,stack=0x10000000,0x10080000
 points=[0x10001000+i*0x100 for i in range(6)]
 def words(a,v):u.mem_write(a,struct.pack('<'+'I'*len(v),*v))
 def call(v):
  for a,p in zip(points,v):words(a,list(map(F,p)))
  words(points[5],[0xdeadbeef]);words(stack,[stop,*points[2:]])
  u.reg_write(X.UC_X86_REG_ESP,stack);u.reg_write(X.UC_X86_REG_ECX,points[0]);u.reg_write(X.UC_X86_REG_EDX,points[1])
  u.emu_start(0x6f125aa0,stop,count=1000)
  if u.reg_read(X.UC_X86_REG_EIP)!=stop or u.reg_read(X.UC_X86_REG_ESP)!=stack+20:raise ValueError('original kernel ABI/budget differs')
  return dict(input=[list(map(F,p))for p in v],hit=u.reg_read(X.UC_X86_REG_EAX),distance=struct.unpack('<I',u.mem_read(points[5],4))[0])
 cases=[];rng=random.Random(229)
 for n in range(256):
  x,y=[rng.uniform(-8192,8192)for _ in range(2)];s=rng.uniform(.125,1024)
  tri=[[x,y,rng.uniform(-256,512)],[x+s,y,rng.uniform(-256,512)],[x,y+s,rng.uniform(-256,512)]]
  for a,b in ((0,0),(0,1),(1,0),(.5,.5),(.3,.4),(-.001,.5),(1.001,0)):
   cases.append(call([[x+a*s,y+b*s,2560],[0,0,-1],*tri]))
 for epsilon in [2**-24,2**-23,2**-22,2**-21,2**-20]:
  for reverse in [False,True]:
   tri=[[0,0,0],[1,0,10],[0,epsilon,20]]
   if reverse:tri[1],tri[2]=tri[2],tri[1]
   for point in [[0,0,2560],[.25,epsilon*.25,2560],[.5,epsilon*.5,-2560],[1,epsilon,2560]]:
    cases.append(call([point,[0,0,-1],*tri]))
 for direction in [[0,0,1],[0,0,-1],[0,1,-1],[1,0,-1],[0,0,0]]:
  cases.append(call([[0,0,-2560],direction,[-1,-1,0],[1,-1,10],[0,1,20]]))
 transforms=[];distances=[]
 def invoke(entry,ecx,edx,args,ret):
  words(stack,[stop,*args]);u.reg_write(X.UC_X86_REG_ESP,stack)
  u.reg_write(X.UC_X86_REG_ECX,ecx);u.reg_write(X.UC_X86_REG_EDX,edx)
  u.emu_start(entry,stop,count=1000)
  if u.reg_read(X.UC_X86_REG_EIP)!=stop or u.reg_read(X.UC_X86_REG_ESP)!=stack+4+ret:raise ValueError('original ABI/budget differs')
 for i in range(512):
  v=[rng.uniform(-2048,2048)for _ in range(3)]
  m=[rng.uniform(-4,4)for _ in range(9)]+[rng.uniform(-8192,8192)for _ in range(3)]
  words(points[0],list(map(F,v)));words(points[1],list(map(F,m)))
  invoke(0x6f1c8230,points[2],points[0],[points[1]],4)
  transforms.append(dict(input=list(map(F,v)),matrix=list(map(F,m)),output=list(struct.unpack('<3I',u.mem_read(points[2],12)))))
 for i in range(518):
  if i<6:
   p=[[0,0,2560],[0,0,-2560],[0,0,2561],[0,0,-2561],[1,1,0],[1,1,-4000]][i];a=[0,0,2560];b=[0,0,-2560]
  else:p,a,b=[[rng.uniform(-8192,8192)for _ in range(3)]for _ in range(3)]
  for ptr,v in zip(points,[p,a,b]):words(ptr,list(map(F,v)))
  invoke(0x6f18a670,points[0],points[1],[points[2],points[3],points[4]],12)
  distances.append(dict(input=[list(map(F,v))for v in [p,a,b]],distance=struct.unpack('<I',u.mem_read(points[3],4))[0],fraction=struct.unpack('<I',u.mem_read(points[4],4))[0]))
 return dict(sha256=SHA,entry='6f125aa0',cases=cases,transforms=transforms,distances=distances)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 r=original(a.binary);a.report.write_text(json.dumps(r,indent=1)+'\n');print('original mesh cases',len(r['cases']),'hits',sum(c['hit']for c in r['cases']))
if __name__=='__main__':main()
