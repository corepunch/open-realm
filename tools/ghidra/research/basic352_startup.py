"""Invoke the199 data-rooted scalar thunks with poisoned destinations and real CRT digit code."""
import ctypes as C,hashlib,json,struct
from pathlib import Path
import verify_wc3_pathing_numeric as n
from generate_wc3_math_tables import reciprocal_table

def witness(binary,library,rows):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
 from unicorn.x86_const import (UC_X86_REG_ESP as SP,UC_X86_REG_EIP as IP,
  UC_X86_REG_ECX as CX,UC_X86_REG_EDX as DX,UC_X86_REG_EAX as AX,
  UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP)
 raw=binary.read_bytes();assert hashlib.sha256(raw).hexdigest()=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
 pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24;base,size=(struct.unpack_from('<I',raw,opt+i)[0]for i in (28,56))
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  s=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,count,off=struct.unpack_from('<III',raw,s+12)
  if count:u.mem_write(base+va,raw[off:off+count])
 u.mem_map(0x20000000,0x10000);stack,stop=0x20008000,0x30000000
 def wr(at,words):u.mem_write(at,struct.pack('<'+'I'*len(words),*(v&0xffffffff for v in words)))
 def rd(at,count=1):return list(struct.unpack('<'+'I'*count,u.mem_read(at,count*4)))
 n.initialize_runtime_scalars(u,stack,stop)
 from wc3_shipped_crt import load_crt
 crt=load_crt(u,binary.parent/'msvcr120.dll');wr(0x6fa7c4fc,[crt['exports']['isdigit']])
 lib=C.CDLL(str(library.resolve()));lib.pathing_integer_float.restype=C.c_uint32;lib.pathing_decimal.restype=C.c_uint32
 preserved=[UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP];records=[]
 for row in rows:
  context=row['context'];root=context[2];entry=int(row['entry'],16);assert int(root['address'],16)==entry
  assert context[0]['asm']=='JMP 0x'+row['target']and context[1]['asm'].startswith('MOV ECX,0x')
  destination=int(context[1]['asm'].split(',')[1],16);first=root['asm']
  source=0xffffffff if first=='OR EDX,0xffffffff'else 0 if first=='XOR EDX,EDX'else int(first.split(',')[1],16)
  for table in root['data_roots']:assert rd(int(table,16))[0]==entry
  if row['target']=='6f070d80':
   model=n.integer_float(source);engine=lib.pathing_integer_float(C.c_uint32(source));input_value=source
  else:
   text=bytearray();at=source
   while u.mem_read(at,1)[0]:text.extend(u.mem_read(at,1));at+=1
   input_value=text.decode('ascii');model=n.decimal_bits(input_value,reciprocal_table());engine=lib.pathing_decimal(C.c_char_p(bytes(text)))
  assert engine==model
  left,right=rd(destination-4)[0],rd(destination+4)[0];wr(destination,[0xabcdef01]);wr(stack,[stop]);u.reg_write(SP,stack);u.reg_write(CX,0xdeadbeef);u.reg_write(DX,0xfeed0000)
  for reg in preserved:u.reg_write(reg,0x12340000+reg)
  u.emu_start(entry,stop,count=50000)
  assert u.reg_read(IP)==stop and u.reg_read(SP)==stack+4 and u.reg_read(AX)==destination
  assert all(u.reg_read(reg)==0x12340000+reg for reg in preserved)
  assert rd(destination)[0]==model and rd(destination-4)[0]==left and rd(destination+4)[0]==right
  records.append(dict(entry=row['entry'],tables=root['data_roots'],destination=destination,input=input_value,output=model))
 return dict(cases=len(records),integer_thunks=sum(isinstance(r['input'],int)for r in records),decimal_thunks=sum(isinstance(r['input'],str)for r in records),outcome_sha256=hashlib.sha256(json.dumps(records,separators=(',',':')).encode()).hexdigest(),records=records)
