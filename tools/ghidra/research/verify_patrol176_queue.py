#!/usr/bin/env python3
"""Execute the complete original5fccc0 queued-command predicate without stand-ins.

The active head is ignored. Commands851990/851991 preserve the found flag;
any other queued command writes1 and ends iteration. This is the classifier,
not an emulation of the user-list traversal or entire Patrol task graph.
"""
import argparse,hashlib,itertools,json,struct
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'

def execute(binary):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
 from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX,UC_X86_REG_EIP
 raw=binary.read_bytes();assert hashlib.sha256(raw).hexdigest()==SHA
 pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24;base,size=(struct.unpack_from('<I',raw,opt+n)[0]for n in(28,56))
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 u.mem_write(base,raw[:struct.unpack_from('<I',raw,opt+60)[0]])
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  sec=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,n,ofs=struct.unpack_from('<III',raw,sec+12)
  if n:u.mem_write(base+va,raw[ofs:ofs+n])
 u.mem_map(0x20000000,0x20000);order,state,stack,stop=0x20001000,0x20002000,0x20010000,0x30000000
 def w(a,v):u.mem_write(a,struct.pack('<I',v))
 rows=[]
 for command,head,previous in itertools.product((851990,851991,851986,851983,851972,851993,0,0xffffffff),range(2),range(2)):
  w(order+0x24,command);w(state,order if head else 0);w(state+4,previous);w(stack,stop)
  u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,order);u.reg_write(UC_X86_REG_EDX,state)
  u.emu_start(0x6f5fccc0,stop,count=1000)
  assert u.reg_read(UC_X86_REG_EIP)==stop and u.reg_read(UC_X86_REG_ESP)==stack+4
  found=struct.unpack('<I',u.mem_read(state+4,4))[0];continued=u.reg_read(UC_X86_REG_EAX)
  assert continued==int(head or command in (851990,851991))
  assert found==(previous if continued else 1)
  rows.append(dict(command=command,head=head,previous=previous,continued=continued,found=found))
 return dict(binary_sha256=SHA,scope=__doc__,rows=rows)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in('binary','output','expected','fixture'):p.add_argument('--'+name,type=Path,required=name in('binary','output'))
 a=p.parse_args();assert not a.output.exists();result=execute(a.binary)
 if a.expected:assert result==json.loads(a.expected.read_text())
 if a.fixture:a.fixture.write_text(json.dumps(result,indent=2)+'\n')
 report=dict(passed=True,status='retail-patrol-queue-predicate',cases=len(result['rows']),binary_sha256=SHA)
 a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
