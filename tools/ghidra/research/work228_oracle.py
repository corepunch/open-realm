#!/usr/bin/env python3
"""Execute original SSE flyer interpolation and transposed maximum filter.

Original inline-vector allocation and security-cookie paths run unchanged.
No data-return stubs or rewritten arithmetic. Widths stay within inline capacity.
"""
import argparse,hashlib,json,random,struct
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
F=lambda n:struct.unpack('<I',struct.pack('<f',n))[0]

def original(binary):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn import x86_const as X
 b=Path(binary).read_bytes()
 if hashlib.sha256(b).hexdigest()!=SHA:raise ValueError('wrong game.dll')
 pe=struct.unpack_from('<I',b,60)[0];opt=pe+24;base,size=[struct.unpack_from('<I',b,opt+n)[0]for n in(28,56)]
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 for i in range(struct.unpack_from('<H',b,pe+6)[0]):
  s=opt+struct.unpack_from('<H',b,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',b,s+12)
  if n:u.mem_write(base+va,b[off:off+n])
 u.mem_map(0x10000000,0x100000);u.mem_map(0x20000000,0x10000)
 stop,stack,t,g,src,dst,pt=[0x10000000+n for n in (0,0x80000,0x1000,0x2000,0x3000,0x10000,0x2100)]
 def words(a,v):u.mem_write(a,struct.pack('<'+'I'*len(v),*v))
 def read(a,n):return list(struct.unpack('<'+'I'*n,u.mem_read(a,n*4)))
 sampled=[]
 def before_float_return(uc,address,size,user):
  sampled.append(read(uc.reg_read(X.UC_X86_REG_EBP)+12,1)[0])
 u.hook_add(UC_HOOK_CODE,before_float_return,begin=0x6f743902,end=0x6f743902)
 def call(entry,args,ecx,edx=0):
  u.mem_write(stop,b'\x90')
  words(stack,[stop,*args]);u.reg_write(X.UC_X86_REG_ESP,stack);u.reg_write(X.UC_X86_REG_ECX,ecx);u.reg_write(X.UC_X86_REG_EDX,edx)
  u.reg_write(X.UC_X86_REG_FPSW,0);u.reg_write(X.UC_X86_REG_FPTAG,0xffff)
  end=stop
  u.emu_start(entry,end,count=30000000)
  if u.reg_read(X.UC_X86_REG_EIP)!=end:raise ValueError('original instruction budget '+hex(u.reg_read(X.UC_X86_REG_EIP))+' args='+str(args))
  if u.reg_read(X.UC_X86_REG_ESP)!=stack+4+len(args)*4:raise ValueError('ABI differs')
 rng=random.Random(228);out=dict(sha256=SHA,maximum=[],samples=[])
 for w in (2,3,7,17,64,129,257,1024):
  for radius in (0,1,6,w+3):
   h=3;data=[F(rng.choice((-17,-0.0,0.0,1.25,31,128,256)))for _ in range(w*h)]
   words(src,data);call(0x6f74d4b0,[w,h,radius],src,dst)
   out['maximum'].append(dict(width=w,height=h,radius=radius,input=data,output=read(dst,w*h)))
 data=[F(rng.uniform(-512,512))for _ in range(8*5)];words(src,data)
 words(t+0xc8,[F(-352)]);words(t+0xc4,[F(224)]);words(g,[8,5,F(96),F(96),0,40,src])
 for x in (-600,-400,-352,-351,-304,-256,-128,0,400,2000):
  for y in (-100,223,224,225,304,464,600,1024):
   words(pt,[F(x),F(y)]);call(0x6f743810,[g,pt],t)
   out['samples'].append(dict(point=[F(x),F(y)],result=sampled[-1]))
 out['sample_grid']=dict(width=8,height=5,cell=[96,96],origin=[-352,224],bits=data)
 return out

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--binary',type=Path,required=True);a.add_argument('--report',type=Path,required=True);o=a.parse_args();r=original(o.binary);o.report.write_text(json.dumps(r,indent=1)+'\n');print('original maximum cases',len(r['maximum']),'interpolation cases',len(r['samples']))
if __name__=='__main__':main()
