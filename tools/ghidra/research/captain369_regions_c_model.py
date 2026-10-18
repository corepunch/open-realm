#!/usr/bin/env python3
"""Compare the production C region kernel with actual retail query inputs.
This analysis-only gate does not claim engine producer or complete journey parity.
"""
import argparse,ctypes as c,json,hashlib
from pathlib import Path

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--library',type=Path,required=True);p.add_argument('--capture',type=Path,required=True);p.add_argument('--origin',type=float,nargs=2,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('fresh report required')
 rows=[json.loads(l)for l in a.capture.read_text().splitlines()];built=next(r for r in rows if r.get('event')=='region-built');expected=built['state'];filtered=next(r for r in rows if r.get('event')=='region-filtered');width,height=built['inputWords'][:2]
 lib=c.CDLL(str(a.library.resolve()));P=c.c_void_p;U=c.c_uint32;F=c.c_float;fp=c.POINTER(F);up=c.POINTER(U);bp=c.POINTER(c.c_uint8)
 signatures={'region_alloc':([],P),'region_free':([P],None),'region_build':([P,U,U,bp],c.c_int),'region_count':([P],U),'region_grid':([P,bp],None),'region_nodes':([P,up],None),'region_mark':([P,fp,fp,F],None),'region_expand':([P],None),'region_nearest':([P,fp,fp,F],U),'region_filter':([P,U,fp,fp,F],None),'region_lists':([P,up],U),'region_roll':([P,up,fp,F,fp],c.c_int),'region_first_roll':([up],U)}
 for name,(args,ret)in signatures.items():fn=getattr(lib,name);fn.argtypes=args;fn.restype=ret
 g=lib.region_alloc();assert g
 try:
  origin=(F*2)(*a.origin);classes=(c.c_uint8*(width*height))(*(w>>30 for w in built['cells'][1::2]));assert lib.region_build(g,width,height,classes)
  scale=F.from_buffer_copy(U(expected['scale'])).value
  from captain369_regions_model import scalar
  sources=[r for r in rows if r.get('event')=='region-source-position']
  for r in sources:
   if r.get('type')!='captain':lib.region_mark(g,(F*2)(*map(scalar,r['words'][:2])),origin,scale)
  lib.region_expand(g);count=lib.region_count(g);assert count==expected['count']
  nodes=(U*(count*16))();lib.region_nodes(g,nodes);assert list(nodes)==expected['regions'],'node words differ'
  grid=(c.c_uint8*65536)();lib.region_grid(g,grid);assert list(grid)==expected['grid'],'grid bytes differ'
  actor=next(r for r in sources if r.get('type')=='captain');nearest=lib.region_nearest(g,(F*2)(*map(scalar,actor['words'][:2])),origin,scale);assert nearest==filtered['selected']
  before=next(r for r in rows if r.get('event')=='region-consumer-begin')['point'];point=(F*2)(*map(scalar,before))
  lib.region_filter(g,nearest,point,origin,scale);lists=(U*4098)();lib.region_lists(g,lists);eligible=list(lists[2:2+lists[0]]);near=list(lists[2+lists[0]:2+lists[0]+lists[1]])
  assert eligible==filtered['state']['eligible']and near==filtered['state']['near'],'destination lists differ'
  lib.region_nodes(g,nodes);assert list(nodes)==filtered['state']['regions'],'query visitation differs'
  draws=[r for r in rows if r.get('event')=='purpose13-state'];assert len(draws)==2
  state=(U*2)(*draws[0]['before']);assert lib.region_first_roll(state)==1 and list(state)==draws[0]['after']
  assert lib.region_roll(g,state,origin,scale,point)and list(state)==draws[1]['after']
  result=list(c.cast(point,up)[:2]);expected_point=next(r for r in rows if r.get('event')=='region-random-point')['point'];assert result==expected_point
  report=dict(passed=True,status='c-kernel-parity-only',library_sha256=hashlib.sha256(a.library.read_bytes()).hexdigest(),capture_sha256=hashlib.sha256(a.capture.read_bytes()).hexdigest(),nodes=count,node_words=count*16,grid_bytes=65536,nearest=nearest,eligible=eligible,near=near,random_draws=2,random_state=list(state),point_words=result,scope=__doc__)
  a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
 finally:lib.region_free(g)
if __name__=='__main__':main()
