#!/usr/bin/env python3
"""Execute native Alt attachment, readiness and physical publication in candidate order."""
import argparse,itertools,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_rig import Rig

def original(binary):
 rows=[]
 for float_mask,flight,grounded,alt in itertools.product((0,2,5,15),(0,6,15),(0,1),(0,1)):
  r=Rig(binary,128,128);e=r.e;e.w(r.owner+0x234,r.map)
  acc=e.fixture(0x400);e.w(acc+0x1c,*(m[0]for m in r.levels));e.w(r.owner+0x250,acc)
  e.w(acc+0x5c,e.fixture(4096*36));e.w(acc+0x68,4096,0)
  e.w(acc+0x7c,e.fixture(16384*12));e.w(acc+0x88,16384,0)
  e.uc.mem_write(r.owner+0x54,struct.pack('<fIf',10,0,300))
  vm=e.fixture(0x400);e.w(vm+0x3e0,1);e.w(0x6fd687a8,vm)
  ctx=e.fixture(28);wrappers=[];requests=[]
  for i in range(3):
   w=e.fixture(16);e.call(0x6f21e600,w);e.w(w+4,1);e.call(0x6f89c890,w)
   wrappers.append(w);requests.append(e.call(0x6f054530,e.r(w+8),edx=e.r(w+12)))
   e.w(ctx+4*i,w);e.call(0x6f89caf0,w,alt)
   e.call(0x6f89cbf0,w,struct.unpack('<I',struct.pack('<f',1536))[0],struct.unpack('<I',struct.pack('<f',1536))[0],0)
  e.w(ctx+24,alt);movers=[];units=[];slots=[]
  for i in range(4):
   e.call(0x6f14ee90,r.xy,1,edx=0);m=e.r(r.xy);movers.append(m)
   e.call(0x6f14ec50,r.xy,1,edx=0);p=e.r(r.xy);e.w(m+0xa8,p)
   e.w(p+0x88,0x200000);e.w(p+0xb4,0x3f000000);e.w(m+0x90,0x3f000000)
   e.uc.mem_write(m+0x70,struct.pack('<fI',8,0));e.uc.mem_write(m+0x78,struct.pack('<4f',16+3*(i&1),16+9*(i//2),0,0))
   u=e.fixture(0x300);units.append(u);e.w(u+0x16c,*e.r(m+0x14,2))
   e.w(u+0x1fc,16 if float_mask&(1<<i)else (2 if flight&(1<<i)else 1),1 if grounded and i==1 else 0)
   e.w(u+0x5c,0x20000000 if flight&(1<<i)else 0)
   e.call(0x6f6b8c10,u,edx=ctx)
   attached=[s for s,q in enumerate(requests)if tuple(e.r(m+0x14,2))in[tuple(e.r(q+0x1c+12*k,2))for k in range(12)]]
   if len(attached)!=1:raise ValueError(('attachment',i,attached))
   slots.append(attached[0])
  births=[];seen=set()
  for i,m in enumerate(movers):
   e.call(0x6f89cd10,wrappers[slots[i]],m)
   for candidate in movers:
    key=tuple(e.r(candidate+0x9c,2))
    if key in seen or key in((0,0),(0xffffffff,0xffffffff)):continue
    seen.add(key);g=e.call(0x6f054530,key[0],edx=key[1])
    n=e.r(g+0x38);data=e.r(g+0x28);ids=[tuple(e.r(data+k*44,2))for k in range(n)]
    births.append(dict(after=i,request=slots[i],flags=e.r(g+0x80),members=[next(k for k,x in enumerate(movers)if tuple(e.r(x+0x14,2))==identity)for identity in ids]))
  rows.append(dict(float_mask=float_mask,flight=flight,grounded=grounded,alt=alt,slots=slots,births=births))
 return dict(rows=rows)
def header(spec):
 out=['#ifndef RETAIL_SELECTED_REQUEST240_H','#define RETAIL_SELECTED_REQUEST240_H',
      '/* Complete native FLOAT/primary/Alt-flight attachment and readiness. */',
      'static struct {uint32_t float_mask,flight,grounded,alt,count,sizes[3],members[4],requests[3],flags[3];} const selected240_rows[]={']
 fmt=lambda a:'{'+','.join(str(v)for v in a)+'}'
 for r in spec['rows']:
  groups=[b['members']for b in r['births']];pad=3-len(groups)
  out.append('{'+','.join(str(r[k])for k in('float_mask','flight','grounded','alt'))+','+str(len(groups))+','+fmt([len(g)for g in groups]+[0]*pad)+','+fmt(sum(groups,[]))+','+fmt([b['request']for b in r['births']]+[0]*pad)+','+fmt([b['flags']for b in r['births']]+[0]*pad)+'},')
 return '\n'.join(out+['};','#endif',''])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 s=original(a.binary);a.report.write_text(json.dumps(s,indent=2)+'\n');print(json.dumps(dict(rows=len(s['rows']))))
if __name__=='__main__':main()
