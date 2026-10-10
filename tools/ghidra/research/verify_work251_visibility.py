#!/usr/bin/env python3
"""Run original visibility policy bodies with controlled TLS, player masks and pose.

66fdd0,1dd920,1ddff0,1ddee0,1e0b80 and699b20 execute unmodified. Only TLS
lookup, owner/bridge virtual access, canonical detection-mask access and CRT
cookie validation are boundary stand-ins. This is not a complete world oracle.
"""
import hashlib,itertools,json,struct
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
def verify(binary):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EAX,UC_X86_REG_ECX
 raw=binary.read_bytes()
 if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('original differs')
 pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24;base,size=(struct.unpack_from('<I',raw,opt+x)[0]for x in(28,56));u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  at=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',raw,at+12)
  if n:u.mem_write(base+va,raw[off:off+n])
 for a,n in((0,4096),(0x10000000,0x10000),(0x20000000,0x10000),(0x30000000,4096)):u.mem_map(a,n)
 observer,target,world,vision,player,tls,slot,record,vt,planes=(0x10000000+i*0x800 for i in range(10));stack=0x20008000;stop=0x30000000;ownerfn=stop+0x100;bridgefn=stop+0x200;policy={};trace=[]
 def w(a,*v):u.mem_write(a,struct.pack('<'+'I'*len(v),*(x&0xffffffff for x in v)))
 def word(a):return struct.unpack('<I',u.mem_read(a,4))[0]
 def ret(purge=0,value=None):
  esp=u.reg_read(UC_X86_REG_ESP)
  if value is not None:u.reg_write(UC_X86_REG_EAX,value)
  u.reg_write(UC_X86_REG_EIP,word(esp));u.reg_write(UC_X86_REG_ESP,esp+4+purge)
 def hook(uc,a,n,data):
  esp=uc.reg_read(UC_X86_REG_ESP)
  if a==0x6f06c180:
   if uc.reg_read(UC_X86_REG_ECX)!=13:raise ValueError('TLS selector')
   ret(value=tls)
  elif a==ownerfn:ret(value=3 if uc.reg_read(UC_X86_REG_ECX)==observer else policy['owner'])
  elif a==bridgefn:ret(value=target)
  elif a==0x6f05bc20:w(word(esp+4),8,8);ret(4)
  elif a==0x6f684700:
   if word(esp+4)!=1:raise ValueError('detection selector')
   ret(4,8 if policy['detect'] else 0)
  elif a==0x6f78e855:ret()
  elif a in(0x6f1dd920,0x6f1ddff0,0x6f1ddee0,0x6f699b20):trace.append(a)
 u.hook_add(UC_HOOK_CODE,hook)
 w(observer,vt);w(target,vt);w(vt+0xec,ownerfn);w(vt+0xb8,bridgefn)
 w(world+0x34,vision);w(world+0x54,16)
 for i in range(16):w(world+0x58+4*i,player)
 w(vision+0x10,1,1);w(vision+0x2c,planes);w(vision+0x30,planes+0x100);w(vision+0x60,4);w(vision+0x68,2);w(vision+0x6c,4)
 w(tls+0x10,slot);w(slot,record)
 rows=[]
 for flags,tlsbit,gate,invisible,own,detect,reveal,state,mode in itertools.product(range(8),range(2),range(3),range(2),range(2),range(2),range(3),(1,2,4),(4,7)):
  policy.update(owner=3 if own else 4,detect=detect);w(0x6fd687a8,0 if gate==0 else world);w(world+0x3e0,gate==2);w(record+4,0x200 if tlsbit else 0)
  w(player+0x2e0,8);w(target+0x5c,0x1000000 if invisible else 0);w(target+0x148,8 if reveal==1 else 0);w(target+0x14c,8 if reveal==2 else 0)
  # Exact original vision-state decoder: visible mask / explored mask.
  u.mem_write(planes,struct.pack('<16H',*([8 if state==1 else 0]*16)));u.mem_write(planes+0x100,struct.pack('<16H',*([8 if state==4 else 0]*16)))
  w(stack,stop,target,flags,mode);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,observer);trace.clear()
  u.emu_start(0x6f66fdd0,stop,count=10000)
  if u.reg_read(UC_X86_REG_EIP)!=stop or u.reg_read(UC_X86_REG_ESP)!=stack+16:raise ValueError('ABI/budget differs')
  actual=u.reg_read(UC_X86_REG_EAX);effective=flags|tlsbit
  expected=gate==2 and (reveal!=0 or (((effective&2)!=0 or not invisible or own or detect)and((effective&1)!=0 or(state&mode)!=0)))
  if actual!=expected:raise ValueError(str((flags,tlsbit,gate,invisible,own,detect,reveal,state,mode,actual,expected,trace)))
  rows.append([flags,tlsbit,gate,invisible,own,detect,reveal,state,mode,actual])
 return dict(passed=True,binary_sha256=SHA,fields=['flags','tls','world_gate','invisible','owner_in_mask','detection_in_mask','reveal','vision_state','mode','result'],cases=rows)
if __name__=='__main__':
 import argparse
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('new output required')
 result=verify(a.binary);a.output.write_text(json.dumps(result,separators=(',',':'))+'\n');print(json.dumps(dict(passed=True,cases=len(result['cases']))))
