#!/usr/bin/env python3
"""Run the original nine-word comparator across each key and signed-wrap boundaries."""
import argparse,json,random,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_spatial_harness_copy import Emu

def original(binary):
 e=Emu(binary);units=[e.fixture(0x200)for _ in range(2)];rows=[e.fixture(36)for _ in range(2)];cases=[]
 base=[0,0,0,0,2,0,1]
 pairs=[]
 for key in range(7):
  for value in(1,2,0x7fffffff,0x80000000,0xffffffff):
   a=base.copy();b=base.copy();b[key]=value;pairs.extend(((a,b),(b,a)))
 rng=random.Random(241)
 for _ in range(128):pairs.append(([rng.randrange(4)for _ in range(7)],[rng.randrange(4)for _ in range(7)]))
 for left,right in pairs:
  for u,p,k in zip(units,rows,(left,right)):
   e.w(u+0x198,k[0]);e.w(u+0xc,k[6]);e.w(p,u,*k[1:6],0,0,0)
  result=e.call(0x6f6bcc40,0,*rows)
  if e.esp_after!=4:raise ValueError('comparator ABI differs')
  cases.append(dict(left=left,right=right,result=result))
 return dict(cases=cases)
def header(spec):
 fmt=lambda a:'{'+','.join(f'0x{x:08x}u'for x in a)+'}'
 out=['#ifndef RETAIL_POINT_RANK241_H','#define RETAIL_POINT_RANK241_H',
 '/* Original6bcc40 lexicographic result, including signed32 subtraction wrap. */',
 'static struct {uint32_t left[7],right[7],result;} const rank241_cases[]={']
 for c in spec['cases']:out.append('{'+fmt(c['left'])+','+fmt(c['right'])+f',0x{c["result"]:08x}u'+'},')
 return '\n'.join(out+['};','#endif',''])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 s=original(a.binary);a.report.write_text(json.dumps(s,indent=2)+'\n');print(json.dumps(dict(cases=len(s['cases']))))
if __name__=='__main__':main()
