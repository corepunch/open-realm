#!/usr/bin/env python3
"""Execute original canonical target/member publication, not a behavior model.

Uses Work248's supplied hierarchy/VM storage. Only inherited Storm allocation
adapters replace executable functions. Target, alias and nested exclusion state
are deliberately constructed fixtures, not claims of public Captain inputs.
"""
import argparse,itertools,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_rig import Rig

def original(binary,observe=True):
 from unicorn import UC_HOOK_CODE
 rows=[]
 for outer,drop,target_kind in itertools.product((0,3),(False,True),('none','distinct','alias','invalid','cached','retired')):
  r=Rig(binary,128,128);e=r.e;e.w(r.owner+0x234,r.map)
  acc=e.fixture(0x400);e.w(acc+0x1c,*(m[0]for m in r.levels));e.w(r.owner+0x250,acc)
  e.w(acc+0x5c,e.fixture(4096*36));e.w(acc+0x68,4096,0)
  e.w(acc+0x7c,e.fixture(16384*12));e.w(acc+0x88,16384,0)
  e.uc.mem_write(r.owner+0x54,struct.pack('<fIf',10,0,300))
  vm=e.fixture(0x400);e.w(vm+0x3e0,1);e.w(0x6fd687a8,vm)
  wrapper=e.fixture(16);e.call(0x6f21e600,wrapper);e.w(wrapper+4,1);e.call(0x6f89c890,wrapper)
  req=e.call(0x6f054530,e.r(wrapper+8),edx=e.r(wrapper+12));e.call(0x6f89cbf0,wrapper,0x44c00000,0x44c00000,0)
  movers=[]
  for i in range(4):
   e.call(0x6f14ee90,r.xy,1,edx=0);m=e.r(r.xy);movers.append(m)
   e.call(0x6f14ec50,r.xy,1,edx=0);p=e.r(r.xy);e.w(m+0xa8,p)
   e.w(p+0x88,0x200000);e.w(p+0x9c,0x02000002);e.w(p+0xb4,0x3f000000);e.w(m+0x90,0x3f000000)
   e.uc.mem_write(m+0x70,struct.pack('<fI',8,0));e.uc.mem_write(m+0x78,struct.pack('<4f',16+3*i,16,0,0))
   e.w(e.r(m+0x98)+0x40,outer)
   if i<3:e.call(0x6f89c7f0,wrapper,m)
  target=movers[0]if target_kind=='alias'else movers[3]
  if target_kind!='none':
   identity=e.r(target+0x14,2)
   if target_kind=='invalid':identity[1]+=1
   e.w(req+0xdc,*identity)
  cached=0
  if target_kind in('cached','retired'):
   cached=r.new_object();e.w(cached+0x40,outer);e.w(req+0xf4,cached)
   if target_kind=='retired':e.w(cached+0x38,0xffffffff)
  trace=[];returns=[]
  def state():
   attached=[i for i,m in enumerate(movers[:3])if tuple(e.r(m+0x14,2))in[tuple(e.r(req+0x1c+12*k,2))for k in range(12)]]
   return dict(attached=attached,counters=[e.r(e.r(m+0x98)+0x40)for m in movers],ready=[i for i,m in enumerate(movers[:3])if m in e.r(req+0xac,12)],cached_counter=e.r(cached+0x40)if cached else None,cached_present=bool(e.r(req+0xf4)))
  def hook(uc,addr,size,data):
   if returns and addr==returns[-1][0]:
    ret,phase=returns.pop();trace.append(dict(at=phase,**state()))
   if addr in(0x6f169c50,0x6f169d60,0x6f16da60):
    phase={0x6f169c50:'acquire',0x6f169d60:'release',0x6f16da60:'target-'+str(e.r(uc.reg_read(e.X.UC_X86_REG_ESP)+4))}[addr]
    trace.append(dict(at=phase+'-enter',**state()))
    returns.append((e.r(uc.reg_read(e.X.UC_X86_REG_ESP)),phase+'-leave'))
   elif addr==0x6f16b7b0:trace.append(dict(at='bind',**state()))
  if observe:e.uc.hook_add(UC_HOOK_CODE,hook)
  events=[]
  for kind,i in(('ready',1),('drop'if drop else'ready',2),('ready',0)):
   before=len(trace);e.call(0x6f89cd10 if kind=='ready'else 0x6f89ca80,wrapper,movers[i]);events.append(dict(kind=kind,unit=i,trace=trace[before:],state=state()))
  rows.append(dict(outer=outer,drop=drop,target=target_kind,events=events))
 return dict(rows=rows)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--unobserved',action='store_true');a=p.parse_args()
 if a.report.exists():p.error('new report required')
 s=original(a.binary,not a.unobserved);a.report.write_text(json.dumps(s,indent=2)+'\n');print(json.dumps(dict(rows=len(s['rows']))))
if __name__=='__main__':main()
