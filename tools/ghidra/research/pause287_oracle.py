#!/usr/bin/env python3
"""Execute original scripted-pause stage bodies with labelled helper endpoints.

The helpers record call arguments and the original body's already committed
flags/depth. They do not emulate admission, ability disabling or event handlers;
those owners are independently observed in the read-only retail captures.
"""
import argparse,json
from pathlib import Path
from foot03_spatial_harness_copy import Emu

def original(binary):
 from unicorn import UC_HOOK_CODE
 result=[]
 for kind,address in [('begin',0x6f697770),('end',0x6f6977c0),('pop',0x6f678590),('suspend',0x6f69c490),('release',0x6f69c580)]:
  for flags in [0,1,0x100000,0x200000,0x300001]:
   for depth in [0,1,2,0xffffffff]:
    e=Emu(binary);unit=e.fixture(0x300);e.w(unit+0x54,depth);e.w(unit+0x5c,flags);calls=[]
    endpoints={0x6f6977a0:('disable',0),0x6f6977f0:('enable',0),0x6f688d30:('push',1),0x6f69c620:('schedule',2),0x6f693d50:('refresh',0),0x6f688d90:('acquire',0),0x6f6785c0:('release',0)}
    def hook(uc,a,size,data):
     name,argc=endpoints[a];sp=uc.reg_read(e.X.UC_X86_REG_ESP)
     if uc.reg_read(e.X.UC_X86_REG_ECX)!=unit:raise ValueError('helper receiver differs')
     args=[e.r(sp+4+4*i)for i in range(argc)];calls.append([name,e.r(unit+0x54),e.r(unit+0x5c),args])
     if name=='push'and args!=[0]:raise ValueError('suspension flags differ')
     if name=='schedule'and args!=[0x6fcd53a4,0]:raise ValueError('resume request differs')
     uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp));uc.reg_write(e.X.UC_X86_REG_ESP,sp+4+4*argc)
    for a in endpoints:e.uc.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
    e.call(address,unit)
    f=flags;d=depth;expected=[]
    def event(name,args=[]):expected.append([name,d,f,args])
    if kind=='begin'and not flags&0x200000:
     f|=0x200000
     if f&0x100000:event('disable')
     event('push',[0])
    elif kind=='end'and flags&0x200000:
     if f&0x100000:event('enable')
     d=(d-1)&0xffffffff
     if not d and f&0x100000:event('schedule',[0x6fcd53a4,0])
     f&=~0x200000
    elif kind=='pop':
     d=(d-1)&0xffffffff
     if not d and f&0x100000:event('schedule',[0x6fcd53a4,0])
    elif kind=='suspend':
     event('refresh')
     if 0<d<0x80000000:
      f|=0x100000;event('acquire');f&=~1
      if f&0x200000:event('disable')
    elif kind=='release':
     event('refresh')
     if f&0x100000:f&=~0x100000;event('release')
    if calls!=expected or e.r(unit+0x54)!=d or e.r(unit+0x5c)!=f or e.esp_after!=4:raise ValueError('original stage store/call order differs '+kind)
    result.append(dict(stage=kind,flags=flags,depth=depth,calls=calls,final_flags=f,final_depth=d,stack_bytes=e.esp_after))
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 a.report.write_text(json.dumps(original(a.binary),indent=2)+'\n')
