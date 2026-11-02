"""Analysis only: original bucket preference suffix with constructed canonical units.
Starts after native RollDice; tests its two branch outcomes. No fabricated policy calls.
Stops before publication/transport. Native identity resolution and Unit casts run.
"""
import struct,hashlib,json,itertools,time,argparse
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument("--binary",type=Path,required=True);ap.add_argument("--report",type=Path,required=True);ap.add_argument("--exhaustive",action="store_true");args=ap.parse_args();assert not args.report.exists(),"fresh report required"
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *
p=args.binary;raw=p.read_bytes();assert hashlib.sha256(raw).hexdigest()=='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24;base,size=(struct.unpack_from('<I',raw,opt+n)[0]for n in(28,56));u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095);u.mem_write(base,raw[:struct.unpack_from('<I',raw,opt+60)[0]])
for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
 sec=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,n,o=struct.unpack_from('<III',raw,sec+12)
 if n:u.mem_write(base+va,raw[o:o+n])
u.mem_map(0,0x1000);u.mem_map(0x10000000,0x10000);u.mem_map(0x20000000,0x10000)
cap=0x10000000;registry=cap+0x1000;slots=cap+0x1100;vt=cap+0x1400;stack=0x20008000
w=lambda a,*v:u.mem_write(a,struct.pack('<%dI'%len(v),*(x&0xffffffff for x in v)))
r=lambda a:struct.unpack('<I',u.mem_read(a,4))[0]
w(0x6fd68610,registry);w(registry+0xc,slots);w(registry+0x1c,12);w(vt+0x1c,0x6f684ba0)
for i in range(12):
 wrapper=cap+0x2000+i*0x80;unit=cap+0x4000+i*0x300
 w(slots+i*8,-2,wrapper);w(wrapper+0x18,1);w(wrapper+0xc,0x2b61676c);w(wrapper+0x20,0);w(wrapper+0x54,unit);w(unit,vt);w(unit+4,100)
selected=[]
def end(machine,address,size,data):
 selected.append(None if address==0x6f9d6c9f else (machine.reg_read(UC_X86_REG_EDI),machine.reg_read(UC_X86_REG_ESI)));machine.emu_stop()
u.hook_add(UC_HOOK_CODE,end,begin=0x6f9d69a4,end=0x6f9d69a4);u.hook_add(UC_HOOK_CODE,end,begin=0x6f9d6c9f,end=0x6f9d6c9f)
def call(mask,flags,random):
 u.mem_write(stack-0x200,bytes(0x300))
 for i in range(12):
  w(cap+0x144+i*12,*((i,1,0)if mask>>i&1 else(0xffffffff,0xffffffff,0)));w(cap+0x4000+i*0x300+4,100)
 w(cap+0x6c,flags);w(stack-0x2c,random);w(0,0);selected.clear()
 for reg,val in ((UC_X86_REG_EBP,stack),(UC_X86_REG_ESP,stack-0x100),(UC_X86_REG_EBX,cap),(UC_X86_REG_EDI,0)) :u.reg_write(reg,val)
 u.emu_start(0x6f9d613d,0x6f9d6ca0,count=20000)
 assert len(selected)==1,selected
 return (None,0) if selected[0]is None else ((selected[0][0]-cap-0x4000)//0x300,selected[0][1])
rows=[]
for flags,rand in itertools.product((0,0x80,0x100,0x180,0x200,0x280),(0,1)):
 # Derive native preferences by repeatedly removing its selected bucket.
 mask=4095;order=[]
 while True:
  chosen,unreachable=call(mask,flags,rand)
  if chosen is None:break
  assert 0<=chosen<12 and mask>>chosen&1,(chosen,mask)
  order.append([chosen,unreachable]);mask&=~(1<<chosen)
 rows.append(dict(flags=flags,nonOneRoll=rand,order=order))
cases=0
if args.exhaustive:
 for flags,rand in itertools.product((0,0x80,0x100,0x180,0x200,0x280),(0,1)):
  first,second=(0,1)if rand else(1,0)
  order=([first,second,2,first+6,second+6,8]if flags&0x80 else[2,first,second,8,first+6,second+6])
  if flags&0x300:order=[j for i in order for j in(i+3,i)]
  for mask in range(4096):
   chosen,transport=call(mask,flags,rand);expected=next((i for i in order if mask>>i&1),None)
   assert chosen==expected,(flags,rand,mask,chosen,expected)
   assert transport==int(chosen is not None and chosen%6>=3),(flags,rand,mask,chosen,transport)
   cases+=1
report={'passed':True,'status':'analysis-only','binary_sha256':hashlib.sha256(raw).hexdigest(),'scope':__doc__,'cases':cases,'rows':rows}
args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items()if k!='rows'}))
