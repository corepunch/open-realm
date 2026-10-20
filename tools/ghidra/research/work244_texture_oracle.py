#!/usr/bin/env python3
"""Execute only original21e8f0..21e93e's complete transposition/category loop."""
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
 u.mem_map(0x10000000,0x10000);owner,source,output,stack=0x10000100,0x10001000,0x10002000,0x10003000
 def words(a,v):u.mem_write(a,struct.pack('<'+'I'*len(v),*v))
 pixels=[list(v) for v in itertools.product((0,1,2,127,128,255),repeat=3)]
 # Materialized normalized BGRA image; decoding/allocators are not executed.
 words(owner+8,[36,6]);words(owner+0x20,[output]);words(stack-0x20,[source]);u.mem_write(source,bytes(v for p in pixels for v in(*p,255)))
 for reg,value in [(X.UC_X86_REG_EDI,owner),(X.UC_X86_REG_EBX,owner+8),(X.UC_X86_REG_ESI,0),(X.UC_X86_REG_EAX,owner+12),(X.UC_X86_REG_EBP,stack)]:u.reg_write(reg,value)
 u.emu_start(0x6f21e8f0,0x6f21e93e,count=15000)
 if u.reg_read(X.UC_X86_REG_EIP)!=0x6f21e93e:raise ValueError('loop did not complete')
 return dict(width=36,height=6,bgra=pixels,categories=list(u.mem_read(output,216)),flags=struct.unpack('<I',u.mem_read(owner+0x64,4))[0])
