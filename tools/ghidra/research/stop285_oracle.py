#!/usr/bin/env python3
"""Execute original forced-position wrappers and Stop factory bridge.

Supplied handle resolvers, immediate-order allocation/admission, owner virtual
and placement virtual record their arguments. This verifies wrapper order and
ABI, not the internals of these supplied gameplay operations.
"""
import argparse,json
from pathlib import Path
from foot03_spatial_harness_copy import Emu

def original(binary):
 from unicorn import UC_HOOK_CODE
 result=[]
 for loc in (False,True):
  for valid_unit,valid_location in ((True,True),(False,True),(True,False))if loc else((True,True),(False,True)):
   e=Emu(binary);unit=e.fixture(0x400);table=e.fixture(0x200);location=e.fixture(0x30);xy=e.fixture(8);order=e.fixture(0x40)
   e.w(unit,table);e.w(table+0xec,0x6f000100);e.w(table+0x180,0x6f000200)
   words=[0x44480000,0x44484000];e.w(xy,*words);e.w(location+0x24,*words);calls=[]
   def hook(uc,address,size,data):
    sp=uc.reg_read(e.X.UC_X86_REG_ESP);ecx=uc.reg_read(e.X.UC_X86_REG_ECX);edx=uc.reg_read(e.X.UC_X86_REG_EDX);pop=0;out=0
    if address==0x6f1eef90:
     if ecx!=0x1234:raise ValueError('unit resolver handle differs')
     calls.append(dict(operation='unit-resolve'));out=unit if valid_unit else 0
    elif address==0x6f1eda50:
     if ecx!=0x5678:raise ValueError('location resolver handle differs')
     calls.append(dict(operation='location-resolve'));out=location if valid_location else 0
    elif address==0x6f000100:
     if ecx!=unit:raise ValueError('owner receiver differs')
     calls.append(dict(operation='owner'));out=7
    elif address==0x6f690930:
     calls.append(dict(operation='factory',order=ecx,owner=edx,target=e.r(sp+4)));out=order;pop=4
    elif address==0x6f680320:
     if ecx!=unit or e.r(sp+4)!=order:raise ValueError('admission owner differs')
     calls.append(dict(operation='admit',mode=e.r(sp+8),dispatch=e.r(sp+12)));pop=12
    elif address==0x6f000200:
     if ecx!=unit:raise ValueError('placement owner differs')
     args=e.r(sp+4,10);calls.append(dict(operation='place',xy=[e.r(args[0]),e.r(args[1])],flags=args[2:]));pop=40
    uc.reg_write(e.X.UC_X86_REG_EAX,out);uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp));uc.reg_write(e.X.UC_X86_REG_ESP,sp+4+pop)
   for a in (0x6f1eef90,0x6f1eda50,0x6f000100,0x6f690930,0x6f680320,0x6f000200):e.uc.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
   if loc:e.call(0x6f215620,0,0x1234,0x5678)
   else:e.call(0x6f2155c0,0,0x1234,xy,xy+4)
   if e.esp_after!=4:raise ValueError('native is not plain RET')
   result.append(dict(loc=loc,valid_unit=valid_unit,valid_location=valid_location,calls=calls,stack_bytes=e.esp_after))
 return result

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 a.report.write_text(json.dumps(original(a.binary),indent=2)+'\n')
