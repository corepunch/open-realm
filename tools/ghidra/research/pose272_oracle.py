#!/usr/bin/env python3
"""Execute66d990 and shipped CRT unchanged on captured crossing inputs.

Labelled callee boundaries supply authored UI, heading and captured support
samples. No replacement of orientation, clamping, matrix or CRT arithmetic.
"""
import hashlib,struct,random
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
CRT_SHA='86e39b5995af0e042fcdaa85fe2aefd7c9ddc7ad65e6327bd5e7058bc3ab615f'
def original(binary,poses):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn import x86_const as X
 u=Uc(UC_ARCH_X86,UC_MODE_32)
 def write(a,*v):u.mem_write(a,struct.pack('<'+'I'*len(v),*(n&0xffffffff for n in v)))
 def read(a,n=1):return list(struct.unpack('<'+'I'*n,u.mem_read(a,n*4)))
 def load(path,expected,new=None):
  b=Path(path).read_bytes()
  if hashlib.sha256(b).hexdigest()!=expected:raise ValueError('unsupported PE')
  pe=struct.unpack_from('<I',b,60)[0];opt=pe+24
  old,size=[struct.unpack_from('<I',b,opt+n)[0]for n in (28,56)];base=old if new is None else new
  u.mem_map(base,(size+4095)&~4095);u.mem_write(base,b[:struct.unpack_from('<I',b,opt+60)[0]])
  for i in range(struct.unpack_from('<H',b,pe+6)[0]):
   s=opt+struct.unpack_from('<H',b,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',b,s+12)
   if n:u.mem_write(base+va,b[off:off+n])
  if base!=old:
   start,n=struct.unpack_from('<II',b,opt+96+5*8);p=base+start
   while p<base+start+n:
    page,count=read(p,2)
    if not count:break
    for item in struct.unpack('<'+'H'*((count-8)//2),u.mem_read(p+8,count-8)):
     if item>>12==3:
      at=base+page+(item&4095);write(at,read(at)[0]+base-old)
     elif item>>12:raise ValueError('unsupported relocation')
    p+=count
  return base,base+struct.unpack_from('<I',b,opt+96)[0]
 load(binary,SHA);base,export=load(Path(binary).parent/'msvcr120.dll',CRT_SHA,0x50000000)
 count,functions,names,ordinals=read(export+24,4)
 imports={'_libm_sse2_sin_precise':0x6fa7c48c,'_libm_sse2_cos_precise':0x6fa7c47c,'_libm_sse2_sqrt_precise':0x6fa7c4f8,'_CIatan2':0x6fa7c450,'_libm_sse2_asin_precise':0x6fa7c44c}
 resolved=set()
 for i in range(count):
  a=base+read(base+names+i*4)[0];name=bytes(u.mem_read(a,100)).split(b'\0',1)[0].decode()
  if name in imports:
   ordinal=struct.unpack('<H',u.mem_read(base+ordinals+i*2,2))[0];write(imports[name],base+read(base+functions+ordinal*4)[0]);resolved.add(name)
 if resolved!=set(imports):raise ValueError('missing CRT exports')
 u.mem_map(0x10000000,0x100000)
 stop,stack,unit,vt,heading,headingvt,output,point,slots= [0x10000000+n for n in (0,0x80000,0x1000,0x2000,0x3000,0x3100,0x4000,0x4100,0x5000)]
 write(unit,vt);write(vt+0xb8,stop+0x100);write(heading,headingvt);write(headingvt+0x1c,stop+0x110)
 u.mem_write(stop+0x100,b'\xb8'+struct.pack('<I',heading)+b'\xc3')
 u.mem_write(stop+0x110,b'\x8b\x44\x24\x04\x8b\x15'+struct.pack('<I',slots+12)+b'\x89\x10\xc2\x04\x00')
 for a,slot,ret in [(0x6f34d3f0,slots,0),(0x6f34d410,slots+4,0),(0x6f33f950,slots+8,0),(0x6f78d1e0,slots+16,12)]:
  u.mem_write(a,b'\xd9\x05'+struct.pack('<I',slot)+(b'\xc2'+struct.pack('<H',ret)if ret else b'\xc3'))
 # Ground non-structure predicate boundary, no unknown flags or flyer bank.
 u.mem_write(0x6f68c190,b'\x31\xc0\xc2\x04\x00')
 current={};samples=[];angles=[]
 def observe_angles(machine,address,size,data):
  sp=machine.reg_read(X.UC_X86_REG_ESP);angles.append(read(sp+4,3))
 u.hook_add(UC_HOOK_CODE,observe_angles,begin=0x6f1c6c40,end=0x6f1c6c40)
 def on_support(machine,address,size,data):
  sp=machine.reg_read(X.UC_X86_REG_ESP);args=read(sp+4,3);i=len(samples)
  expected=current['pose']['samples'][i]
  if args[:2]!=expected['point']or args[2]!=1 or machine.reg_read(X.UC_X86_REG_ECX)!=0 or machine.reg_read(X.UC_X86_REG_EDX)!=0:raise ValueError('native sample point/layer differs')
  write(slots+16,expected['height']);samples.append(expected)
 u.hook_add(UC_HOOK_CODE,on_support,begin=0x6f78d1e0,end=0x6f78d1e0)
 out=[]
 for p in poses:
  current['pose']=p;samples.clear();angles.clear();inputs=p['inputs'];write(unit+0x30,p['type']);write(unit+0x5c,p['flags']);write(point,*p['point'])
  write(slots,inputs['pitch'],inputs['roll'],0x41a00000,inputs['heading']);write(stack,stop,output,point)
  u.reg_write(X.UC_X86_REG_ESP,stack);u.reg_write(X.UC_X86_REG_ECX,unit);u.reg_write(X.UC_X86_REG_FPCW,0x27f);u.reg_write(X.UC_X86_REG_FPSW,0);u.reg_write(X.UC_X86_REG_FPTAG,0xffff)
  u.emu_start(0x6f66d990,stop,count=1000000)
  if u.reg_read(X.UC_X86_REG_EIP)!=stop or u.reg_read(X.UC_X86_REG_ESP)!=stack+12 or u.reg_read(X.UC_X86_REG_EAX)!=output:raise ValueError('orientation ABI/budget differs')
  if len(samples)!=len(p['samples'])or read(output,9)!=p['matrix']:raise ValueError('original orientation differs from live '+str(len(out)))
  if angles!=([p['angles']]if 'angles'in p else[]):raise ValueError('original clamp angles differ')
  out.append(read(output,9))
 kernels=[];rng=random.Random(272)
 bits=lambda x:struct.unpack('<I',struct.pack('<f',x))[0]
 for i in range(512):
  xy=[bits(rng.uniform(0,128))for _ in range(2)]
  heights=[bits(rng.uniform(-512,512))for _ in range(4)]
  write(point,*xy,0);write(output,*heights);write(stack,stop,point,output,0,0,0)
  u.reg_write(X.UC_X86_REG_ESP,stack);u.emu_start(0x6f749a30,stop,count=10000)
  if u.reg_read(X.UC_X86_REG_EIP)!=stop or u.reg_read(X.UC_X86_REG_ESP)!=stack+24:raise ValueError('ground-height kernel ABI differs')
  kernels.append(dict(point=xy,heights=heights,result=read(point+8)[0]))
 return dict(cases=len(out),matrix_words=len(out)*9,game_sha256=SHA,crt_sha256=CRT_SHA,ground_kernels=kernels)
