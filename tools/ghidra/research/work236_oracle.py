#!/usr/bin/env python3
"""Execute complete queued callback, real factories, readiness and publication."""
import argparse,itertools,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_rig import Rig
def original(binary):
 cases=[((8,16,90),(0,0,0)),((8,40,72),(0,0,0)),((8,16,56),(0,0,0)),
        ((8,16,57),(0,0,0)),((8,16,57),(.5,0,0)),((8,49,90),(0,0,0))]
 rows=[]
 for case,mask in itertools.product(range(len(cases)),range(8)):
  r=Rig(binary,128,128);e=r.e;e.w(r.owner+0x234,r.map)
  acc=e.fixture(0x400);e.w(acc+0x1c,*(m[0]for m in r.levels));e.w(r.owner+0x250,acc)
  e.w(acc+0x5c,e.fixture(4096*36));e.w(acc+0x68,4096,0)
  e.w(acc+0x7c,e.fixture(16384*12));e.w(acc+0x88,16384,0)
  e.uc.mem_write(r.owner+0x54,struct.pack('<fIf',10,0,300))
  e.call(0x6f14ef80,r.xy,1,edx=0);old=e.r(r.xy)
  movers=[];units=[]
  for i,(x,v)in enumerate(zip(*cases[case])):
   e.call(0x6f14ee90,r.xy,1,edx=0);m=e.r(r.xy);movers.append(m)
   e.call(0x6f14ec50,r.xy,1,edx=0);path=e.r(r.xy);e.w(m+0xa8,path)
   e.w(path+0x88,0x200000 if mask&(1<<i)else 0)
   e.w(path+0xb4,0x3f000000);e.w(m+0x90,0x3f000000)
   e.uc.mem_write(m+0x70,struct.pack('<fI',8,0));e.uc.mem_write(m+0x78,struct.pack('<4f',x,16,v,0))
   unit=e.fixture(0x300);units.append(unit)
   e.w(unit+0x16c,*e.r(m+0x14,2));e.w(unit+0x240,123,456);e.w(unit+0x1fc,1)
   if i:
    e.call(0x6f1691b0,r.xy,m);e.call(0x6f16c060,old+0x1c,r.xy,1);e.call(0x6f170fa0,m,old)
  # Real owner preparation resolves the old rows, not supplied resolved pointers.
  e.call(0x6f16d1c0,old)
  vm=e.fixture(0x400);e.w(vm+0x3e0,1);e.w(0x6fd687a8,vm)
  e.uc.mem_write(old+0x4c,struct.pack('<2f',48,16))
  ctx=e.fixture(40);e.w(ctx,units[0]);e.uc.mem_write(ctx+4,struct.pack('<4f',1536,512,48,16));e.w(ctx+20,123,456,0,0,1)
  before=[e.r(m+0x9c,2)for m in movers]
  spatial=[e.r(m+0x98)for m in movers];flags=[e.r(p+0x40)for p in spatial]
  result=e.call(0x6f5faaf0,units[1],edx=ctx)
  if e.esp_after!=4 or result!=0 or e.r(ctx+0x20)!=1:raise ValueError('callback admission differs')
  wrapper=e.r(ctx+0x1c);request=e.call(0x6f054530,e.r(wrapper+8),edx=e.r(wrapper+12))
  if e.r(request+0xac,3)!=[0xffffffff,*movers[1:]]:raise ValueError('premature readiness publication')
  if [e.r(m+0x9c,2)for m in movers]!=before:raise ValueError('peers moved before source readiness')
  if [e.r(p+0x40)for p in spatial]!=flags:raise ValueError('early exit leaked exclusions')
  e.call(0x6f89cd10,wrapper,movers[0])
  if e.esp_after!=8 or e.r(request+0xac,3)!=[0xffffffff]*3:raise ValueError('source ready ABI/consumption differs')
  if [e.r(p+0x40)for p in spatial]!=flags:raise ValueError('publication leaked exclusions')
  groups=[];seen=set()
  for mover in movers:
   identity=tuple(e.r(mover+0x9c,2))
   if identity in seen:continue
   seen.add(identity);group=e.call(0x6f054530,identity[0],edx=identity[1]);n=e.r(group+0x38);data=e.r(group+0x28)
   ids=[tuple(e.r(data+i*44,2))for i in range(n)]
   groups.append([next(i for i,m in enumerate(movers)if tuple(e.r(m+0x14,2))==key)for key in ids])
  rows.append(dict(case=case,preferred=mask,groups=groups,ready=[-1,1,2],exclusions_restored=True))
 return json.loads(json.dumps(dict(positions=[dict(x=x,velocity=v)for x,v in cases],elapsed=2,rows=rows)))
def header(spec):
 out=['#ifndef RETAIL_QUEUED_PARTITION236_H','#define RETAIL_QUEUED_PARTITION236_H',
      '/* Full original5faaf0 ->89cd10 ->16bcf0/16b7b0, factories and readiness. */',
      'static struct {float x[3],velocity[3];} const queued236_positions[]={']
 fmt=lambda a:'{'+','.join(str(v)for v in a)+'}'
 for c in spec['positions']:out.append('{'+fmt(c['x'])+','+fmt(c['velocity'])+'},')
 out+=['};','static struct {uint32_t position,preferred,count,sizes[3],members[3];} const queued236_rows[]={']
 for r in spec['rows']:
  groups=r['groups'];out.append('{'+str(r['case'])+','+str(r['preferred'])+','+str(len(groups))+','+fmt([len(g)for g in groups]+[0]*(3-len(groups)))+','+fmt(sum(groups,[]))+'},')
 return '\n'.join(out+['};','#endif',''])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 s=original(a.binary);a.report.write_text(json.dumps(s,indent=1)+'\n');print(json.dumps(dict(rows=len(s['rows']))))
if __name__=='__main__':main()
