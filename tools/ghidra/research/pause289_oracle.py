"""Execute original48c8c0/4076e0/40bb00 walks and69dd60 at callback boundaries.

Only identity lookup, constant virtual predicates/validators and terminal refresh
are controlled. Nested inverse calls and validation execute original bodies.
"""
import struct
from foot03_spatial_harness_copy import Emu
from unicorn import UC_HOOK_CODE

def original(binary):
 rows=[]
 for length in [1,2,4,8]:
  for callback in range(length):
   for inverse in [False,True]:
    e=Emu(binary);unit=e.fixture(0x300);owners=[e.fixture(0x100)for _ in range(length)];tables=[e.fixture(0x300)for _ in owners];stub=e.fixture(0x100);calls=[];once=[False];validation=[]
    e.w(unit+0x1dc,1,1)
    for i,(a,v)in enumerate(zip(owners,tables)):
     e.w(a,v);e.w(a+0x24,*([i+2,1]if i+1<length else[0xffffffff,0xffffffff]));e.w(a+0x30,unit)
     for off,address in [(0xcc,0x6f3fa480),(0xe0,0x6f407470),(0xe4,0x6f40b850),(0x2f4,0x6f42bca0),(0xc4,0x6f605aa0),(0xa0,0x6f000100),(0x25c,0x6f000200)]:e.w(v+off,address)
    code=b''.join(b'\x68'+struct.pack('<I',x)for x in [0,0,0,0,851986])+b'\xb9'+struct.pack('<I',unit)+b'\xb8'+struct.pack('<I',0x6f69dd60)+b'\xff\xd0'
    after=stub+len(code)
    if inverse:code+=b''.join(b'\x68'+struct.pack('<I',x)for x in[0,0,0,1])+b'\xb9'+struct.pack('<I',unit)+b'\xb8'+struct.pack('<I',0x6f48c8c0)+b'\xff\xd0'
    e.uc.mem_write(stub,code+b'\xc3')
    def hook(uc,address,size,data):
     sp=uc.reg_read(e.X.UC_X86_REG_ESP);ecx=uc.reg_read(e.X.UC_X86_REG_ECX);value=0;pop=0
     if address==after:
      validation.append(uc.reg_read(e.X.UC_X86_REG_EAX));return
     if address==0x6f061320:
      pair=e.r(ecx,2)
      if pair[1]!=1 or not 1<=pair[0]<=length:raise ValueError('identity lookup differs')
      value=owners[pair[0]-1]
     elif address==0x6f000200:
      pop=20;value=0 if ecx==owners[-1]else 0xdd
     elif address==0x6f000100:pass
     else:
      if ecx not in owners:raise ValueError('refresh receiver differs')
      i=owners.index(ecx);calls.append([i,'off'if address==0x6f605aa0 else'refresh',[[e.r(a+0x3c),e.r(a+0x40)]for a in owners],e.r(unit+0x1c4)])
      if address==0x6f605aa0 and i==callback and not once[0]:
       once[0]=True;uc.reg_write(e.X.UC_X86_REG_EIP,stub);return
     uc.reg_write(e.X.UC_X86_REG_EAX,value);uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp));uc.reg_write(e.X.UC_X86_REG_ESP,sp+4+pop)
    for address in [0x6f061320,0x6f000100,0x6f000200,0x6f605aa0,0x6f401fa0,after]:e.uc.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    e.call(0x6f48c8c0,unit,1,1,0,0)
    final=[[e.r(a+0x3c),e.r(a+0x40)]for a in owners]
    expected=0 if inverse else 1
    if e.esp_after!=20 or final!=[[expected,expected]for _ in owners]or e.r(unit+0x1c4)!=expected or validation!=[0xb9 if callback==length-1 else 0]:raise ValueError('ordered original walk/validation differs')
    rows.append(dict(length=length,callback=callback,inverse=inverse,validation=validation,calls=calls,final=final,unit_depth=e.r(unit+0x1c4),stack=e.esp_after))
 return rows


def classes(binary):
 e=Emu(binary);rows=[]
 for table in [0x6fb28330,0x6fb47bd0,0x6fadb5a4,0x6fb62794]:
  entry=e.r(table+0x1c);code=e.call(entry)
  rows.append(dict(vtable=hex(table-0x6f000000),getter=hex(entry),class_=code.to_bytes(4,'big').decode('ascii')))
 return [dict(vtable=x['vtable'],getter=x['getter'],**{'class':x['class_']})for x in rows]
