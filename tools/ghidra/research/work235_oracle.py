#!/usr/bin/env python3
"""Run unchanged16b7b0 recursion, native rows/binds/prediction/distance consumers."""
import argparse,itertools,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from route01_1_2_harness import Retail,fw

def original(binary):
 r=Retail(binary,256);request=r.alloc(0x200);members=r.alloc(12*0x2c)
 movers=[r.alloc(0x200)for _ in range(3)];paths=[r.alloc(0x200)for _ in movers]
 r.uc.mem_write(r.owner+0x54,struct.pack('<fIf',10,0,300))
 r.write(r.group+0x28,members);r.write(r.group+0x34,12)
 for i,(m,p)in enumerate(zip(movers,paths)):
  r.write(m+0x14,i+10,100+i);r.write(m+0xa8,p);r.write(p+0xb4,fw(.5))
  r.write(m+0x70,fw(8),0)
 cases=[([(8,8),(48.875,8),(90,8)],[(0,0)]*3),
        ([(8,8),(90,8),(48,8)],[(0,0)]*3),
        ([(8,8),(49,8),(90,8)],[(0,0)]*3),
        ([(8,8),(98.875,8),(190,8)],[(0,0)]*3),
        ([(8,8),(49,8),(90,8)],[(.5,0),(0,0),(0,0)]),
        ([(8,8),(20,20),(32,8)],[(0,0)]*3),
        ([(8,8),(88,8),(48,8)],[(0,0)]*3),
        ([(8,8),(20,20),(32,8)],[(0,0)]*3)]
 rows=[]
 for case,flags,mask in itertools.product(range(len(cases)),(0,0x100,0x200),range(8)):
  positions,velocities=cases[case]
  if flags==0 and mask==0:
   r.clear_map()
   if case==7:r.block_fine([(16,y)for y in range(64)],masks=(2,))
   r.rebuild()
  r.write(request+0x100,flags);r.write(request+0xac,*movers,*([0]*9))
  for i,(m,p)in enumerate(zip(movers,paths)):
   r.write(p+0x88,0x200000 if mask&(1<<i)else 0)
   r.write(m+0xd8,0xabcd1234);r.write(m+0x9c,-1,-1)
   r.uc.mem_write(m+0x78,struct.pack('<4f',*positions[i],*velocities[i]))
  groups=[]
  for i in range(3):
   if r.read(request+0xac+i*4)[0]in(0,0xffffffff):continue
   r.write(r.group+0x38,0);r.write(r.group+0x14,50+len(groups),200)
   _,cleanup=r.run(0x6f16b7b0,request,r.group,i)
   if cleanup!=8:raise ValueError('bind ABI differs')
   n=r.read(r.group+0x38)[0]
   groups.append([r.read(members+k*0x2c)[0]-10 for k in range(n)])
  rows.append(dict(case=case,flags=flags,preferred=mask,groups=groups,
   mover_flags=[r.read(m+0xd8)[0]for m in movers],ready=r.read(request+0xac,3)))
 return json.loads(json.dumps(dict(positions=[dict(position=p,velocity=v)for p,v in cases],elapsed=2,rows=rows)))

def header(spec):
 out=['#ifndef RETAIL_GROUP_PARTITION235_H','#define RETAIL_GROUP_PARTITION235_H',
      '/* Complete original16b7b0 including recursive native append/bind/distance. */',
      'static struct {float position[3][2],velocity[3][2];} const partition235_positions[]={']
 fmt=lambda p:'{'+','.join(str(v)for v in p)+'}'
 for c in spec['positions']:out.append('{'+','.join('{'+','.join(fmt(p)for p in c[k])+'}'for k in ('position','velocity'))+'},')
 out+=['};','static struct {uint32_t position,flags,preferred,count,sizes[3],members[3];} const partition235_rows[]={']
 for r in spec['rows']:
  groups=r['groups'];flat=sum(groups,[])
  out.append('{'+','.join(str(r[k])+'u'for k in ('case','flags','preferred'))+','+str(len(groups))+','+fmt([len(g)for g in groups]+[0]*(3-len(groups)))+','+fmt(flat)+'},')
 return '\n'.join(out+['};','#endif',''])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 result=original(a.binary);a.report.write_text(json.dumps(result,indent=1)+'\n');print(json.dumps(dict(rows=len(result['rows']))))
if __name__=='__main__':main()
