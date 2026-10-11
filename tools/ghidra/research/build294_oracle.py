#!/usr/bin/env python3
"""Run original append/dispatch for Build point packets with controlled callbacks.

Actual canonical registry/copy/lookup, append and dispatch branches execute
unmodified. The event receiver and ability execution are supplied boundaries,
not a public Shift Build trajectory or complete construction implementation.
"""
import argparse,json
from pathlib import Path
from foot03_spatial_harness_copy import Emu
from unicorn import UC_HOOK_CODE

def original(binary,cases):
 out=[]
 for case in cases:
  e=Emu(binary);unit=e.fixture(0x300);registry=e.fixture(0x100);slots=e.fixture(0x100);vt=e.fixture(0x100);order_vt=e.fixture(0x100)
  objects=[e.fixture(0x100)for i in range(3)];wrappers=[e.fixture(0x100)for i in objects];sink=e.fixture(0x100)
  ids=[[i,i+100]for i in range(3)];end=[0xffffffff]*2
  e.w(0x6fd68610,registry);e.w(registry+0xc,slots);e.w(registry+0x18,3,3)
  for i,(obj,wrapper)in enumerate(zip(objects,wrappers)):
   e.w(slots+8*i,-2,wrapper);e.w(wrapper+0xc,0x2b61676c);e.w(wrapper+0x14,*ids[i]);e.w(wrapper+0x54,obj)
   e.w(obj,order_vt,1);e.w(obj+0xc,*ids[i]);e.w(obj+0x2c,*end)
  e.w(unit,vt);e.w(vt+0x10,sink);e.w(order_vt+0x1c,0x6f684a90)
  order=objects[1];e.w(order+0x20,0x84,case['command']);e.w(order+0x48,0x42800000);e.w(order+0x50,0x42800000)
  busy=case['busy'];e.w(unit+0x174,*(ids[2]if busy else end));e.w(unit+0x19c,*(ids[0]if busy else end));e.w(unit+0x1a8,*(ids[0]if busy else end));e.w(unit+0x1b4,int(busy))
  trace=[]
  def hook(uc,address,size,data):
   sp=uc.reg_read(e.X.UC_X86_REG_ESP);pop=0;value=0
   if address==sink:
    event=e.r(sp+4);trace.append(dict(event='busy-task-notification',code=e.r(event+8)));pop=4
   elif address==0x6f67c230:
    assert e.r(sp+4)==order
    trace.append(dict(event='point-event',command=e.r(order+0x24),flags=e.r(order+0x20),head=e.r(unit+0x19c,2),internal=e.r(unit+0x174,2)))
    if case['callback']=='nested':e.w(unit+0x174,*ids[2])
    if case['callback']=='stop':e.w(unit+0x19c,*end)
    pop=4
   elif address==0x6f679cc0:
    trace.append(dict(event='ability-execute',command=e.r(order+0x24),head=e.r(unit+0x19c,2)));value=1;pop=4
   else:
    trace.append(dict(event='dispatch-tasks'))
   uc.reg_write(e.X.UC_X86_REG_EAX,value);uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp));uc.reg_write(e.X.UC_X86_REG_ESP,sp+4+pop)
  for a in(sink,0x6f67c230,0x6f679cc0,0x6f67df00):e.uc.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
  e.call(0x6f693490,unit,order);assert e.esp_after==8
  appended=dict(count=e.r(unit+0x1b4),head=e.r(unit+0x19c,2),tail=e.r(unit+0x1a8,2),trace=list(trace))
  if busy:
   assert trace==[dict(event='busy-task-notification',code=0xd02a5)]
   e.w(unit+0x174,*end);e.w(unit+0x19c,*ids[1]);e.w(unit+0x1b4,1);trace.clear()
   e.call(0x6f67abe0,unit,order,1);assert e.esp_after==12
  out.append(dict(**case,append=appended,activation=list(trace),flags=e.r(order+0x20)))
 return out

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--cases',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 a.report.write_text(json.dumps(original(a.binary,json.loads(a.cases.read_text())),indent=2)+'\n')
if __name__=='__main__':main()
