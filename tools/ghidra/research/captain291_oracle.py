"""Execute original CTownSpot refresh with supplied identity/position bridges.

Only external identity resolution, Unit bridge dispatch, position queries and
FloatMini notifications are controlled. The vector arithmetic, square root,
branching and publication order execute unmodified retail instructions.
"""
import argparse,json,struct
from pathlib import Path
from foot03_spatial_harness_copy import Emu
from unicorn import UC_HOOK_CODE

def original(binary, cases):
 rows=[]
 for case in cases:
  e=Emu(binary);spot=e.fixture(0x70);vtable=e.fixture(0xc0);setter=e.fixture(4);bridge_get=e.fixture(4)
  e.w(vtable, setter);e.w(vtable+0xb8,bridge_get)
  for o in (0x28,0x30,0x38,0x40): e.w(spot+o,vtable)
  for o in (0x48,0x54,0x60): e.w(spot+o,-1,-1)
  for o,w in zip((0x2c,0x34,0x3c,0x44),case['before']):e.w(spot+o,w)
  units={};positions={}
  for i,key in enumerate(('building','mine')):
   if case[key] is None: continue
   unit=e.fixture(0x200);e.w(unit,vtable);units[i+1]=unit;positions[unit+0x164]=case[key]
   e.w(spot+(0x48 if i==0 else 0x54),i+1,i+1)
  e.w(0x6fd3c7d8,case['offset']);writes=[];queries=[]
  def hook(uc,address,size,data):
   sp=uc.reg_read(e.X.UC_X86_REG_ESP);ecx=uc.reg_read(e.X.UC_X86_REG_ECX)
   if address==0x6f061320: value=units.get(e.r(ecx),0);pop=0
   elif address==bridge_get: value=ecx+0x164;pop=0
   elif address==0x6f058900:
    out=e.r(sp+4);xyz=positions[ecx];e.w(out,*xyz);queries.append(xyz);value=out;pop=4
   else:
    value=e.r(e.r(sp+4));e.w(ecx+4,value);writes.append([ecx-spot+4,value]);pop=8
   uc.reg_write(e.X.UC_X86_REG_EAX,value);uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp));uc.reg_write(e.X.UC_X86_REG_ESP,sp+4+pop)
  for addr in (0x6f061320,0x6f058900,setter,bridge_get):e.uc.hook_add(UC_HOOK_CODE,hook,begin=addr,end=addr)
  e.call(0x6f9bfe80,spot)
  if e.esp_after!=4:raise ValueError('wrong refresh ABI')
  rows.append(dict(**case,after=[e.r(spot+o)for o in (0x2c,0x34,0x3c,0x44)],writes=writes,queries=queries))
 return rows

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--cases',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 rows=original(a.binary,json.loads(a.cases.read_text()));a.report.write_text(json.dumps(rows,indent=2)+'\n');print('Original CTownSpot refresh:',len(rows))
if __name__=='__main__':main()
