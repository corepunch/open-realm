#!/usr/bin/env python3
"""Execute complete original disabled group-route replacement over retained tables."""
import argparse,itertools,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from route01_1_2_harness import Retail,fw

def original(binary):
 r=Retail(binary);rows=[]
 for cls,history,goal in itertools.product(range(4),(0,1,3),((48,16),(48.125,16.875),(24,32))):
  r.construct_path();r.write(r.path+0x88,cls<<30);r.write(r.path+0x7c,123,456)
  r.write(r.path+0x1c,*(fw(v)for v in goal));r.write(r.path+0x24,*(fw(v)for v in goal))
  r.write(r.points,fw(4),fw(4),fw(9),fw(9),fw(10),fw(10))
  if history:r.run(0x6f1485f0,r.path+0x54,r.points,history)
  r.write(r.path+0x78,0xffffffff);r.set_buckets(0xffffffff,0xffffffff)
  before=r.read(r.path+0x7c,2);buckets=r.read(0x6fd53a90,0x70//4)
  result,cleanup=r.run(0x6f167120,r.path,r.points,1)
  if cleanup!=8:raise ValueError('group route ABI differs')
  if r.read(r.path+0x7c,2)!=before or r.read(0x6fd53a90,0x70//4)!=buckets:raise ValueError('disabled route consumed admission')
  rows.append(dict(cls=cls,history=history,goal=[fw(v)for v in goal],result=result,
   count=r.table(0x54)['count'],index=r.read(r.path+0x78)[0],capacity=r.table(0x54)['capacity'],
   points=r.words(0x54),timestamps=r.read(r.path+0x7c,2)))
 return dict(rows=rows)
def header(spec):
 out=['#ifndef RETAIL_GROUP_DISABLED234_H','#define RETAIL_GROUP_DISABLED234_H',
      '/* Complete original167120 results with exhausted work buckets. */',
      'static struct {uint32_t cls,history,goal[2],count,index,capacity,points[2];} const disabled234_rows[]={']
 for r in spec['rows']:
  words=lambda v:'{'+','.join(str(x)+'u'for x in v)+'}'
  out.append('{'+f'{r["cls"]},{r["history"]},'+words(r['goal'])+','+f'{r["count"]},{r["index"]},{r["capacity"]},'+words(r['points'])+'},')
 return '\n'.join(out+['};','#endif',''])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 result=original(a.binary);a.report.write_text(json.dumps(result,indent=1)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
