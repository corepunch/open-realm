#!/usr/bin/env python3
"""Complete unchanged original group source selection with software prediction."""
import argparse,itertools,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_rig import Rig

def original(binary):
 r=Rig(binary);e=r.e;group=e.fixture(0x200);members=e.fixture(12*0x2c);goal=e.fixture(8)
 e.w(group+0x28,members);e.w(group+0x38,3)
 e.uc.mem_write(r.owner+0x54,struct.pack('<fIf',10,0,300))
 movers=[];paths=[]
 for i in range(3):
  mover=r.mover();path=e.fixture(0x200);e.w(mover+0xa8,path);e.w(members+i*0x2c+0x14,mover)
  e.uc.mem_write(mover+0x70,struct.pack('<fI',8,0));movers.append(mover);paths.append(path)
 e.call(0x6f161040,movers[0],r.xy)
 if e.f(r.xy)!=2:raise ValueError("fixture clock differs")
 rows=[]
 cases=[([(1,1),(4,4),(8,8)],[(0,0)]*3,(10,10)),
        ([(1,1)]*3,[(0,0)]*3,(10,10)),
        ([(1,1),(4,4),(8,8)],[(4,4),(0,0),(-4,-4)],(10,10))]
 for case,(positions,velocities,destination)in enumerate(cases):
  e.uc.mem_write(goal,struct.pack('<2f',*destination))
  for i in range(3):e.uc.mem_write(movers[i]+0x78,struct.pack('<4f',*positions[i],*velocities[i]))
  for flags,mask in itertools.product((0,0x200),range(8)):
   e.w(group+0x80,flags)
   for i in range(3):e.w(paths[i]+0x88,0x200000 if mask&(1<<i)else 0)
   index=e.call(0x6f16c6d0,group,goal)
   if e.esp_after!=8:raise ValueError('source ABI differs')
   rows.append(dict(case=case,flags=flags,preferred=mask,index=index))
 return json.loads(json.dumps(dict(positions=[dict(positions=p,velocities=v,goal=g)for p,v,g in cases],elapsed=2,rows=rows)))
def header(spec):
 out=['#ifndef RETAIL_GROUP_SOURCE233_H','#define RETAIL_GROUP_SOURCE233_H',
      '/* Unchanged original16c6d0 results, including161040 elapsed prediction. */',
      'static struct {float position[3][2],velocity[3][2],goal[2];} const source233_positions[]={']
 for c in spec['positions']:
  fmt=lambda p:'{'+','.join(str(v) for v in p)+'}'
  out.append('{'+','.join(['{'+','.join(fmt(p)for p in c[k])+'}'for k in ('positions','velocities')]+[fmt(c['goal'])])+'},')
 out+=['};','static struct {uint32_t position,flags,preferred,index;} const source233_rows[]={']
 for r in spec['rows']:out.append('{'+','.join(str(r[k])+'u' for k in ('case','flags','preferred','index'))+'},')
 return '\n'.join(out+['};','#endif',''])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 r=original(a.binary);a.report.write_text(json.dumps(r,indent=1)+'\n');print(json.dumps(r))
if __name__=='__main__':main()
