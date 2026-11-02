#!/usr/bin/env python3
"""Execute original Town selection with supplied identity/classification bridges.

External identity lookup, AI-node Unit lookup, structure classification and
admission recording are controlled. All selection branches, encounter order
and policy-bit clearing execute the unmodified original function.
"""
import argparse,json
from pathlib import Path
from foot03_spatial_harness_copy import Emu
from unicorn import UC_HOOK_CODE

def original(binary,cases):
 rows=[]
 for case in cases:
  e=Emu(binary);town=e.fixture(0x300);units=[e.fixture(0x300)for x in case['units']];nodes=[e.fixture(0x40)for x in units];order=case.get('order',list(reversed(range(len(units)))));identities={};by_node={};structures={};admitted=[]
  for i,kind in enumerate(case['units']):
   e.w(units[i]+0x20,1 if kind&4 else 0);e.w(units[i]+0x248,0x10 if kind&1 else 0);structures[units[i]]=1 if kind&2 else 0;by_node[nodes[i]]=units[i];identities[i+1]=nodes[i]
  for k,i in enumerate(order):e.w(nodes[i]+0x24,order[k+1]+1,order[k+1]+1)if k+1<len(order)else e.w(nodes[i]+0x24,-1,-1)
  e.w(town+0x28,order[0]+1,order[0]+1)if order else e.w(town+0x28,-1,-1)
  e.w(town+0x1b8,case.get('spot',-1));e.w(town+0x2d0,0xa5)
  def hook(uc,address,size,data):
   sp=uc.reg_read(e.X.UC_X86_REG_ESP);ecx=uc.reg_read(e.X.UC_X86_REG_ECX);pop=0
   if address==0x6f061320:value=identities.get(e.r(ecx),0)
   elif address==0x6f055cc0:value=by_node.get(ecx,0)
   elif address==0x6f68c190:value=structures[ecx];pop=4
   else:
    admitted.append(units.index(e.r(sp+4)));assert e.r(sp+8)==0;value=0;pop=8
   uc.reg_write(e.X.UC_X86_REG_EAX,value);uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp));uc.reg_write(e.X.UC_X86_REG_ESP,sp+4+pop)
  for a in (0x6f061320,0x6f055cc0,0x6f68c190,0x6f9c01f0):e.uc.hook_add(UC_HOOK_CODE,hook,begin=a,end=a)
  e.call(0x6f9cc6b0,town);assert e.esp_after==4
  rows.append(dict(**case,selected=admitted,flags=e.r(town+0x2d0)))
 return rows

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--cases',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh output required')
 a.report.write_text(json.dumps(original(a.binary,json.loads(a.cases.read_text())),indent=2)+'\n')
if __name__=='__main__':main()
