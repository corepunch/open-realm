#!/usr/bin/env python3
"""Original native validation bodies; controlled ability lookup/virtual answers.

This isolates the original disabled-counter aggregation and core Stop bypass.
It does not substitute a result for any validation body or claim to execute
individual ability validators, their disable producers or full task admission.
"""
import argparse,json
from pathlib import Path
from foot03_spatial_harness_copy import Emu

def original(binary):
 from unicorn import UC_HOOK_CODE
 result=[]
 for shape,address,slot,args in [('immediate',0x6f69dc80,0x264,[851993,7,0]),
                                ('point',0x6f69dd60,0x25c,[851986,7,0,0,0]),
                                ('target',0x6f69df70,0x260,[851986,7,0,0])]:
  for counters in [(0,0,0),(1,0,0),(0,1,0),(0,0,1),(1,1,1),(-1,-1,-1)]:
   for answer in [0,0xb9,0xdc,0xdd]:
    e=Emu(binary);unit=e.fixture(0x300);ability=e.fixture(0x300);table=e.fixture(0x300);xy=e.fixture(8);calls=[]
    e.w(unit+0x1dc,1,7);e.w(ability,table);e.w(ability+0x24,0xffffffff,0xffffffff);e.w(ability+0x3c,*[x&0xffffffff for x in counters]);e.w(table+0xa0,0x6f000100);e.w(table+slot,0x6f000200)
    supplied=list(args)
    if shape=='point':supplied[-2:]=[xy,xy+4]
    if shape=='target':supplied[-1]=unit
    def hook(uc,a,size,data):
     sp=uc.reg_read(e.X.UC_X86_REG_ESP);ecx=uc.reg_read(e.X.UC_X86_REG_ECX);pop=0;value=0
     if a==0x6f061320:
      if ecx!=unit+0x1dc:raise ValueError('unexpected resolver receiver')
      value=ability;calls.append('resolve')
     elif a==0x6f000100:
      if ecx!=ability:raise ValueError('context receiver differs')
      calls.append('context')
     else:
      if ecx!=ability:raise ValueError('validator receiver differs')
      count=3 if shape=='immediate'else 5 if shape=='point'else 4
      expected=[supplied[0],7]+([xy,xy+4]if shape=='point'else[unit]if shape=='target'else[])+[3]
      if e.r(sp+4,count)!=expected:raise ValueError('virtual operand ABI differs')
      pop=count*4;value=answer;calls.append('ability-validation')
     uc.reg_write(e.X.UC_X86_REG_EAX,value);uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp));uc.reg_write(e.X.UC_X86_REG_ESP,sp+4+pop)
    for a in [0x6f061320,0x6f000100,0x6f000200]:e.uc.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
    value=e.call(address,unit,*supplied)
    expected=answer if answer else 0xdd if counters[1]>0 else 0xb9 if counters[0]>0 or counters[2]>0 else 0
    if value!=expected or e.esp_after!=4+4*len(supplied)or calls!=['resolve','context','ability-validation']:raise ValueError('original validation aggregation differs')
    result.append(dict(shape=shape,counters=list(counters),answer=answer,result=value,stack_bytes=e.esp_after,calls=calls))
 for command in [851972,851973]:
  for flags in [0,0x100,0x300200,0x300300]:
   e=Emu(binary);unit=e.fixture(0x300);e.w(unit+0x5c,flags)
   value=e.call(0x6f69dc80,unit,command,7,0)
   expected=0xdd if command==851972 and flags&0x100 else 0
   if value!=expected or e.esp_after!=16:raise ValueError('original core validation bypass differs')
   result.append(dict(shape='core',command=command,flags=flags,result=value,stack_bytes=e.esp_after))
 return result

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 a.report.write_text(json.dumps(original(a.binary),indent=2)+'\n')
