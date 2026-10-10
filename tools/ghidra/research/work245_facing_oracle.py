#!/usr/bin/env python3
"""Execute complete original15f660 facing queries with real scalar callees; no stubs."""
import hashlib,itertools,struct
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
def original(binary):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,x86_const as X
 raw=Path(binary).read_bytes()
 if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('game.dll differs')
 pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24;base,size=[struct.unpack_from('<I',raw,opt+n)[0]for n in(28,56)]
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  s=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',raw,s+12)
  if n:u.mem_write(base+va,raw[off:off+n])
 u.mem_map(0x10000000,0x20000);owner,source,target,angle,stack,stop=0x10000100,0x10001000,0x10002000,0x10002100,0x10018000,0x1001f000
 def words(a,v):u.mem_write(a,struct.pack('<'+'I'*len(v),*(n&0xffffffff for n in v)))
 bits=lambda f:struct.unpack('<I',struct.pack('<f',f))[0]
 words(0x6fd53a48,[owner]);words(0x6fd3c740,[bits(-1),0,bits(1),bits(2)])
 words(owner+0x54,[0,0,bits(300)])
 rows=[]
 for delta,heading,half in itertools.product([(0,0),(1e-5,0),(.001,0),(3,0),(3,4),(-3,4),(-3,-4),(3,-4)],
       [0,0x80000000,0x3e4ccccd,0x3f000000,0x3f000001,0x3f000002,0x40490fdb,0x40c90fdb],
       [0,0x3e4ccccd,0x3f000000,0x3f000001,0x40490fdb]):
  input=[bits(25),bits(25),bits(25+delta[0]),bits(25+delta[1]),heading,half]
  words(source+0x70,[0,0,*input[:2],0,0,0,heading]);words(target,input[2:4]);words(angle,[half]);words(stack,[stop,target,angle])
  u.reg_write(X.UC_X86_REG_ESP,stack);u.reg_write(X.UC_X86_REG_ECX,source)
  u.emu_start(0x6f15f660,stop,count=20000)
  if u.reg_read(X.UC_X86_REG_EIP)!=stop:raise ValueError('facing query did not complete')
  rows.append(dict(input=input,output=u.reg_read(X.UC_X86_REG_EAX)))
 return rows
