#!/usr/bin/env python3
"""Original 499790/4996f0/3ba140 slot predicates and 497da0 counter words.

The counter test stops 401fa0's post-adjustment notification at its boundary;
current weapon is -1, so it does not certify attack cancellation or dispatch.
Slot predicates execute their complete original callees without substitutions.
"""
import argparse, hashlib, json, struct
from pathlib import Path

HASH='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TARGETS=[0,1,2,4,8,16,32,64,128,256,512,1024,66,130,258,65,192,448,2047]

def run(binary):
 from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
 from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_ECX,UC_X86_REG_EAX
 raw=binary.read_bytes()
 if hashlib.sha256(raw).hexdigest()!=HASH:raise ValueError('unsupported retail DLL')
 pe=struct.unpack_from('<I',raw,0x3c)[0];opt=pe+24
 base,size=struct.unpack_from('<I',raw,opt+28)[0],struct.unpack_from('<I',raw,opt+56)[0]
 uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(base,(size+4095)&~4095)
 uc.mem_write(base,raw[:struct.unpack_from('<I',raw,opt+60)[0]])
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  section=opt+struct.unpack_from('<H',raw,pe+20)[0]+i*40
  va,count,offset=struct.unpack_from('<III',raw,section+12)
  if count:uc.mem_write(base+va,raw[offset:offset+count])
 uc.mem_map(0x20000000,0x20000);attack=0x20001000;stack=0x20010000;stop=0x30000000
 def w(a,v):uc.mem_write(a,struct.pack('<I',v&0xffffffff))
 def r(a):return struct.unpack('<I',uc.mem_read(a,4))[0]
 def call(entry,args):
  w(stack,stop)
  for i,v in enumerate(args):w(stack+4+i*4,v)
  uc.reg_write(UC_X86_REG_ESP,stack);uc.reg_write(UC_X86_REG_ECX,attack)
  uc.emu_start(entry,stop,count=10000)
  if uc.reg_read(UC_X86_REG_EIP)!=stop or uc.reg_read(UC_X86_REG_ESP)!=stack+4+4*len(args):raise ValueError('incomplete native call')
  return uc.reg_read(UC_X86_REG_EAX)
 slots=[]
 # Each mask supplies positive counters. Negative/zero counters are checked
 # independently below, including words that a Boolean cache would mishandle.
 for weapon in range(9):
  for target in TARGETS:
   row=[]
   for mask in range(8):
    for i in range(3):w(attack+0x224+i*4,(mask>>i)&1)
    w(attack+0x20,0x180000)
    for slot in range(2):w(attack+0xdc+slot*4,weapon);w(attack+0x218+slot*4,target)
    actual=[call(0x6f499790,[slot]) for slot in range(2)]
    special=target in {1,64,128,256}
    expected=int(not ((mask&1 and weapon==1 and not special) or (mask&2 and 2<=weapon<=8 and not special) or (mask&4 and special)))
    if actual!=[expected,expected]:raise ValueError(('slot mismatch',weapon,target,mask,actual,expected))
    row.append(expected)
   slots.append(dict(weapon=weapon,targets=target,enabled=row))
 disabled=0
 for flags in (0,0x80000,0x100000,0x180000):
  for slot in range(2):
   w(attack+0x20,flags)
   for i in range(3):w(attack+0x224+i*4,0)
   if call(0x6f499790,[slot])!=int(bool(flags&(0x80000<<slot))):raise ValueError('authored flag mismatch')
   disabled+=1
 signed=0
 for counter in (-2147483648,-1,0,1,2147483647):
  for kind,(weapon,target) in enumerate(((1,2),(2,2),(1,64))):
   for i in range(3):w(attack+0x224+i*4,counter if i==kind else 0)
   w(attack+0x20,0x80000);w(attack+0xdc,weapon);w(attack+0x218,target)
   if call(0x6f499790,[0])!=int(counter<=0):raise ValueError('signed predicate mismatch')
   signed+=1
 def notify(uc,address,size,_):
  if address==0x6f401fa0:
   sp=uc.reg_read(UC_X86_REG_ESP);uc.reg_write(UC_X86_REG_EIP,r(sp));uc.reg_write(UC_X86_REG_ESP,sp+4)
 hook=uc.hook_add(UC_HOOK_CODE,notify,begin=0x6f401fa0,end=0x6f401fa0)
 counters=[];w(attack+0x2b8,0xffffffff)
 for initial in (-2147483648,-1,0,1,2147483647):
  for release in (0,1):
   for mask in range(8):
    for i in range(3):w(attack+0x224+i*4,initial)
    call(0x6f497da0,[release,*[(mask>>i)&1 for i in range(3)]])
    actual=[r(attack+0x224+i*4)for i in range(3)]
    expected=[(initial+((-1 if release else 1) if mask&(1<<i) else 0))&0xffffffff for i in range(3)]
    if actual!=expected:raise ValueError('counter word mismatch')
    counters.append(dict(initial=initial,release=release,mask=mask,words=actual))
 uc.hook_del(hook)
 return dict(binary_sha256=HASH,targets=TARGETS,slots=slots,counters=counters,complete_slot_calls=len(slots)*8*2+disabled+signed,complete_counter_calls=len(counters),notification_boundary='401fa0 omitted; current weapon -1, cancellation not exercised')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--expected',type=Path);a=p.parse_args()
 if a.report.exists():p.error('report must be fresh')
 result=run(a.binary)
 if a.expected and result!=json.loads(a.expected.read_text()):raise ValueError('frozen native evidence differs')
 a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=1)+'\n')
 print(json.dumps({k:result[k]for k in ('complete_slot_calls','complete_counter_calls')}))
if __name__=='__main__':main()
