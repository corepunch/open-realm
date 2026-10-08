#!/usr/bin/env python3
"""Run complete retail494350 and its enabled-slot predicates without stand-ins.

Constructed Attack records span both weapon kinds, authored enable masks,
counted melee/ranged/special prevention and ordinary/special target masks.
This proves slot selection, not complete projectile or stat-timing parity.
"""
import argparse,hashlib,itertools,json,struct
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'

def execute(binary):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
 from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_ECX,UC_X86_REG_EAX,UC_X86_REG_EIP
 raw=binary.read_bytes();assert hashlib.sha256(raw).hexdigest()==SHA
 pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24
 base,size=(struct.unpack_from('<I',raw,opt+n)[0]for n in (28,56))
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 u.mem_write(base,raw[:struct.unpack_from('<I',raw,opt+60)[0]])
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  section=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i
  va,n,offset=struct.unpack_from('<III',raw,section+12)
  if n:u.mem_write(base+va,raw[offset:offset+n])
 u.mem_map(0x20000000,0x20000);attack,stack,stop=0x20001000,0x20010000,0x30000000
 def w(a,v):u.mem_write(a,struct.pack('<I',v))
 results=[]
 for enabled,counters,weapon0,weapon1,special in itertools.product(range(4),range(8),range(9),range(9),range(2)):
  w(attack+0x20,enabled<<19)
  for i in range(3):w(attack+0x224+4*i,(counters>>i)&1)
  for slot,weapon in enumerate((weapon0,weapon1)):
   w(attack+0xdc+4*slot,weapon);w(attack+0x218+4*slot,64 if special else 2)
  w(stack,stop);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,attack)
  u.emu_start(0x6f494350,stop,count=10000)
  assert u.reg_read(UC_X86_REG_EIP)==stop and u.reg_read(UC_X86_REG_ESP)==stack+4
  result=u.reg_read(UC_X86_REG_EAX);assert result in (0,1);results.append(result)
 return dict(binary_sha256=SHA,scope=__doc__,dimensions=[4,8,9,9,2],slots=results)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('binary','output','expected','fixture','header'):p.add_argument('--'+name,type=Path,required=name in ('binary','output'))
 a=p.parse_args();assert not a.output.exists(),'report must be fresh';result=execute(a.binary)
 if a.expected:assert result==json.loads(a.expected.read_text()),'native selector changed'
 if a.fixture:a.fixture.write_text(json.dumps(result,indent=1)+'\n')
 if a.header:
  values=result['slots'];lines=[', '.join(map(str,values[i:i+64])) for i in range(0,len(values),64)]
  a.header.write_text('/* Complete original494350, no substituted callees. See attack175-ground-slot.json. */\n'
   'static uint8_t const retail_attack_ground_slots[] = {\n    '+',\n    '.join(lines)+'\n};\n')
 report=dict(passed=True,status='retail-attack-ground-slot',binary_sha256=SHA,cases=len(result['slots']),scope=__doc__)
 a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
