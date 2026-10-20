#!/usr/bin/env python3
"""Complete original point-query scopes; only existing Storm storage adapters."""
import argparse,itertools,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_rig import Rig

def original(binary):
 r=Rig(binary);e=r.e;obj=r.new_object();mover=r.mover();bridge=r.register_mover(mover);e.w(mover+0x98,obj)
 rows=[]
 for flags,mask,terrain,excluded,point in itertools.product([0,1,0x20000000,0x40000000,0x10000000,0x10000001],[0,0x02000000,0x02000002,0x04000004,0x06000006],[0,2],[False,True],[(272,272),(260,284),(304,272),(-.125,272),(512,272)]):
  r.reset_map();e.w(obj+0x34,0x010000ca,7);e.w(obj+0x40,flags)
  r.record(8,8,obj,True);r.record(8,8,obj,True);r.set_terrain(8,8,terrain)
  e.w(r.map+0xb4,1000);e.w(r.sys+0xd4,0x5a5a)
  e.uc.mem_write(r.xy+0x10,struct.pack('<2f',*point));e.w(r.xy+0x18,mask)
  result=e.call(0x6f04df50,r.xy+0x10,r.xy+0x18,bridge if excluded else 0,edx=r.xy+0x14)
  if e.esp_after!=12 or e.r(r.sys+0xd4)!=0x5a5a or e.r(0)!=0:raise ValueError('original scope/mode/SEH restoration differs')
  rows.append(dict(flags=flags,mask=mask,terrain=terrain,excluded=excluded,point=list(point),result=result,stamp=e.r(r.map+0xb4),object_stamp=e.r(obj+0x38),flags_after=e.r(obj+0x40)))
 return rows

def header(rows):
 out=['#ifndef RETAIL_POINT_QUERY231_H','#define RETAIL_POINT_QUERY231_H',
 '/* Original04df50/149320/05bd30 outputs. Storm storage only; no query stubs. */',
 'static struct {uint32_t flags,mask,terrain;bool excluded;float point[2];uint32_t result,stamp,object_stamp,flags_after;} const point_query231[]={']
 for r in rows:
  v=[str(r[k])+'u'for k in('flags','mask','terrain')]+['true'if r['excluded']else'false','{'+','.join(str(x)for x in r['point'])+'}']+[str(r[k])+'u'for k in('result','stamp','object_stamp','flags_after')]
  out.append('{'+','.join(v)+'},')
 return '\n'.join(out+['};','#endif',''])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--header',type=Path);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 r=original(a.binary);a.report.write_text(json.dumps(r,indent=1)+'\n')
 if a.header:a.header.write_text(header(r))
 print('original complete point queries',len(r))
if __name__=='__main__':main()
