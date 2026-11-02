#!/usr/bin/env python3
"""Run original05ca50 ->171340 ->170080, real registry, placement and publication.

Only Storm storage and terrain support-level lookup are supplied. The support
predicate654060 itself runs unchanged. No movement, scope or search is replaced.
"""
import argparse,itertools,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_rig import Rig

def original(binary,observe=True):
 from unicorn import UC_HOOK_CODE
 rows=[]
 for kind,cls,outer in itertools.product(('clear','admitted','exhausted','no_callback'),range(4),range(2)):
  r=Rig(binary,64,64);e=r.e;e.w(r.owner+0x234,r.map)
  e.uc.mem_write(r.owner+0x54,struct.pack('<fIf',10,0,300))
  e.call(0x6f14ee90,r.xy,1,edx=0);m=e.r(r.xy)
  e.call(0x6f14ec50,r.xy,1,edx=0);p=e.r(r.xy);e.w(m+0xa8,p)
  e.w(p+0x88,0x200000);e.w(p+0x9c,0x02000002);e.w(p+0xb4,0x3f000000)
  e.uc.mem_write(m+0x90,struct.pack('<f',.25+.5*cls))
  e.uc.mem_write(m+0x78,struct.pack('<4f',20.5,20.5,0,0))
  spatial=e.r(m+0x98);e.w(spatial+0x40,outer)
  bridge=e.fixture(32);e.w(bridge+8,*e.r(m+0x14,2));context=e.fixture(16)
  if kind=='admitted':
   for y in range(19,22):
    for x in range(19,22):e.w(r.cells+4*(y*64+x),0x02ffffff)
  if kind=='exhausted':
   for y in range(8,33):
    for x in range(8,33):e.w(r.cells+4*(y*64+x),0x02ffffff)
  trace=[];callback_calls=0
  def hook(uc,address,size,data):
   nonlocal callback_calls
   if address==0x6f78bc90:
    callback_calls+=1
    sp=uc.reg_read(e.X.UC_X86_REG_ESP)
    uc.reg_write(e.X.UC_X86_REG_EAX,0);uc.reg_write(e.X.UC_X86_REG_ESP,sp+8);uc.reg_write(e.X.UC_X86_REG_EIP,e.r(sp))
   if observe and address in(0x6f171340,0x6f170080,0x6f149370,0x6f14a1e0,0x6f05c820,0x6f168b80):
    trace.append([address-0x6f000000,e.r(spatial+0x40),e.r(r.sys+0xd4)])
  e.uc.hook_add(UC_HOOK_CODE,hook)
  e.w(r.sys+0xd4,7)
  e.call(0x6f05ca50,bridge,0 if kind=='no_callback'else 0x6f654060,context)
  if e.esp_after!=12 or e.r(spatial+0x40)!=outer or e.r(r.sys+0xd4)!=7 or e.r(0)!=0:raise ValueError('scope/ABI not restored')
  row=dict(kind=kind,cls=cls,outer=outer,pose=e.r(m+0x78,4),counter=e.r(spatial+0x40),mode=e.r(r.sys+0xd4),path=e.r(p+0x1c,4),flags=e.r(p+0x88),callback_calls=callback_calls)
  if observe:row['trace']=trace
  rows.append(row)
 return dict(rows=rows,limits=['Terrain support lookup78bc90 returns supplied level0; original654060 predicate executes.',
  'Factories, canonical identity,05ca50,171340,170080, fine footprint/search and fine-point publication execute original instructions.',
  'No public notification reentrancy or nonflat support geometry is claimed.'])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 result=original(a.binary);control=original(a.binary,False)
 if [{k:v for k,v in r.items()if k!='trace'}for r in result['rows']]!=control['rows']:raise ValueError('observer changed results')
 result['controls']=control['rows'];a.report.write_text(json.dumps(result,indent=1)+'\n');print(json.dumps(dict(rows=len(result['rows']),controls=len(control['rows']))))
if __name__=='__main__':main()
