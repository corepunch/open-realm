#!/usr/bin/env python3
"""Original651590 unit scope and063d10 lists, using real canonical mover storage.

Unit wrapper and canonical unit tag/liveness are supplied fixture inputs; the
unit factory and public notification path are not claimed. The supplied vtable
uses original6864d0 bridge getter. Registry lookup,05bd30 and063d10 execute
unchanged. Only the rig's Storm memory imports are host storage adapters.
"""
import argparse,itertools,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_rig import Rig

def original(binary,observe=True):
 from unicorn import UC_HOOK_CODE
 rows=[]
 for policy,count,outer in itertools.product(('live','wrong_tag','retired','stale'),(0,4),(0,3)):
  r=Rig(binary,32,32);e=r.e;e.w(r.owner+0x234,r.map)
  e.call(0x6f14ee90,r.xy,1,edx=0);m=e.r(r.xy);record=e.r(m+0x98)
  # This fixture gives an original canonical record the unit-wrapper tag.
  # Its separate bridge identity still resolves the same original mover.
  unit=e.fixture(0x300);vt=e.fixture(0x200);e.w(unit,vt)
  e.w(vt+0xb8,0x6f6864d0);e.w(unit+0xc,*e.r(m+0x14,2));e.w(unit+0x16c,*e.r(m+0x14,2))
  e.w(m+0xc,0 if policy=='wrong_tag'else 0x2b61676c);e.w(m+0x20,int(policy=='retired'))
  if policy=='stale':e.w(unit+0xc,0xffffffff,0xffffffff)
  objects=[r.new_static_object(unit)for _ in range(count)]
  for p in objects:e.w(p+0x40,0x10000000+outer)
  if count:
   a=e.fixture(4*count);e.w(a,*objects);lst=e.fixture(16);e.w(lst+4,count,a);e.w(unit+0x34,lst)
  e.w(record+0x40,outer);trace=[]
  def state():return [e.r(record+0x40),*[e.r(p+0x40)for p in objects]]
  def hook(uc,addr,size,data):
   if addr in(0x6f05bd30,0x6f063d10):trace.append([addr-0x6f000000,state()])
  if observe:e.uc.hook_add(UC_HOOK_CODE,hook)
  before=state();e.call(0x6f651590,unit,1)
  if e.esp_after!=8:raise ValueError('acquire ABI')
  held=state();e.call(0x6f651590,unit,0)
  if e.esp_after!=8 or state()!=before:raise ValueError('release/ABI')
  if held!=[v+int(policy=='live')for v in before]:raise ValueError('canonical gate')
  row=dict(policy=policy,count=count,outer=outer,before=before,held=held,after=state())
  if observe:row['trace']=trace
  rows.append(row)
 # Null slots and aliased pointers are legal063d10 inputs. It skips nulls and
 # increments repeated identities per occurrence; no deduplication is allowed.
 r=Rig(binary);e=r.e;obj=r.new_static_object();e.w(obj+0x40,0x10000003)
 lst=e.fixture(16);a=e.fixture(16);e.w(a,obj,0,obj,0);e.w(lst+4,4,a)
 before=e.r(obj+0x40);e.call(0x6f063d10,lst,1);held=e.r(obj+0x40)
 e.call(0x6f063d10,lst,0);after=e.r(obj+0x40)
 if (before,held,after)!=(0x10000003,0x10000005,0x10000003):raise ValueError('null/alias list')
 return dict(rows=rows,null_alias=[before,held,after],limits=[__doc__.strip()])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh output required')
 out=original(a.binary);control=original(a.binary,False)
 if [{k:v for k,v in r.items()if k!='trace'}for r in out['rows']]!=control['rows']:raise ValueError('observer effect')
 out['controls']=control;a.report.write_text(json.dumps(out,indent=2)+'\n');print('16 original scope cases,16 controls and null/alias list verified')
if __name__=='__main__':main()
