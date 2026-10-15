"""Execute original decimal formatting; delegate only the two integer sprintf layouts.

All scalar arithmetic, width/precision branches and decimal scaling run in the
original DLL. The byte layouts are independently witnessed by public retail R2SW.
"""
import ctypes as C,hashlib,json,struct
from pathlib import Path
import verify_wc3_pathing_numeric as n

def witness(binary,library,cases):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn.x86_const import (UC_X86_REG_ESP as SP,UC_X86_REG_EIP as IP,
  UC_X86_REG_ECX as CX,UC_X86_REG_EDX as DX,UC_X86_REG_EAX as AX,
  UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP)
 raw=binary.read_bytes();assert hashlib.sha256(raw).hexdigest()=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
 pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24;base,size=(struct.unpack_from('<I',raw,opt+i)[0]for i in (28,56))
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  s=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,count,off=struct.unpack_from('<III',raw,s+12)
  if count:u.mem_write(base+va,raw[off:off+count])
 u.mem_map(0x10000000,0x10000);u.mem_map(0x20000000,0x10000);u.mem_map(0x30000000,0x1000)
 stack,stop,arg,out,stub=0x20008000,0x30000000,0x10000100,0x10001000,0x30000010
 def wr(at,words):u.mem_write(at,struct.pack('<'+'I'*len(words),*(v&0xffffffff for v in words)))
 def rd(at,count=1):return list(struct.unpack('<'+'I'*count,u.mem_read(at,count*4)))
 def string(at):
  result=bytearray()
  for i in range(512):
   ch=u.mem_read(at+i,1)[0]
   if not ch:return result.decode('ascii')
   result.append(ch)
  raise ValueError('unterminated original string')
 n.initialize_runtime_scalars(u,stack,stop);wr(0x6fa7c500,[stub]);delegations=0
 signed=lambda word:word-0x100000000 if word&0x80000000 else word
 def integer_printf(machine,address,size,user):
  nonlocal delegations
  if address!=stub:return
  sp=u.reg_read(SP);ret,destination,fmt=rd(sp,3);form=string(fmt)
  if form=='%d':text=str(signed(rd(sp+12)[0]))
  elif form=='%*d.%0*d':
   width,whole,places,fraction=map(signed,rd(sp+12,4));assert 0<=width<=256 and 0<=places<=9
   text=str(whole).rjust(width)+'.'+str(fraction).zfill(places)
  else:raise ValueError('unexpected delegated format '+repr(form))
  u.mem_write(destination,text.encode()+b'\0');u.reg_write(AX,len(text));u.reg_write(CX,0xdeadbeef);u.reg_write(DX,0xfeed0000);u.reg_write(SP,sp+4);u.reg_write(IP,ret);delegations+=1
 u.hook_add(UC_HOOK_CODE,integer_printf)
 lib=C.CDLL(str(library.resolve()));lib.pathing_format_real.argtypes=[C.c_uint32,C.c_int32,C.c_int32,C.POINTER(C.c_char),C.c_uint32];lib.pathing_format_real.restype=C.c_int
 preserved=[UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP];records=[]
 for word,width,precision in cases:
  wr(arg-4,[0xabcdef01,word,0xabcdef02]);u.mem_write(out-4,struct.pack('<I',0xabcdef03)+b'\xcc'*256+struct.pack('<I',0xabcdef04));wr(stack,[stop,width,precision])
  u.reg_write(SP,stack);u.reg_write(CX,arg);u.reg_write(DX,out)
  for reg in preserved:u.reg_write(reg,0x12340000+reg)
  u.emu_start(0x6f0701d0,stop,count=200000)
  assert u.reg_read(IP)==stop and u.reg_read(SP)==stack+12
  assert all(u.reg_read(reg)==0x12340000+reg for reg in preserved)
  assert rd(arg-4,3)==[0xabcdef01,word,0xabcdef02]
  assert rd(out-4)[0]==0xabcdef03 and rd(out+256)[0]==0xabcdef04
  actual=string(out);buffer=C.create_string_buffer(256)
  ok=lib.pathing_format_real(word,width,precision,buffer,256)
  assert ok and buffer.value.decode()==actual,(hex(word),width,precision,actual,buffer.value.decode(),ok)
  records.append([word,width,precision,actual])
 return dict(cases=len(records),integer_layout_boundaries=delegations,outcome_sha256=hashlib.sha256(json.dumps(records,separators=(',',':')).encode()).hexdigest(),records=records)
