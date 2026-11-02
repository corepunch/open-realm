#!/usr/bin/env python3
"""Unmodified058900 ->05b1c0(selector0), supplied mover/registry/clock state.

No code substitutions, game calls through Frida, or Storm calls. Querying does
not publish either mover. The target velocity deliberately differs from source.
"""
import itertools,struct
from foot03_spatial_harness_copy import Emu

def bits(f):return struct.unpack('<I',struct.pack('<f',f))[0]
def original(binary):
 e=Emu(binary)
 registry=e.fixture(0x100);slots=e.fixture(0x20);game=e.fixture(0x100);owner=e.fixture(0x100)
 source=e.fixture(0x200);target=e.fixture(0x200);a=e.fixture(0x20);b=e.fixture(0x20);out=e.fixture(0x20)
 e.w(0x6fd68610,registry);e.w(registry+0xc,slots);e.w(registry+0x1c,2)
 e.w(slots,-2,source,-2,target);e.w(source+0x14,0,100);e.w(target+0x14,1,101)
 e.w(a+8,0,100);e.w(b+8,1,101);e.w(0x6fd3c82c,game);e.w(0x6fd53a48,owner)
 assert e.call(0x6f054530,0,edx=100)==source
 assert e.call(0x6f054530,1,edx=101)==target
 rows=[]
 for origin,point,wrapped in itertools.product([(0,0),(-8192.25,4096.125),(16777216,-8388608),(3.3,-7.7)],[(.0312500037,2.2),(-.5,6.7)],[False,True]):
  now,old,epoch=(.25,7.75,1)if wrapped else(.25,0,0)
  source_fine=list(map(bits,point));target_fine=list(map(bits,(5.125,8.75)));velocity=list(map(bits,(7.3,-1.7)))
  e.w(game+0x6c,*map(bits,origin));e.w(source+0x70,bits(old),0,*source_fine,*velocity)
  e.w(target+0x70,bits(old),0,*target_fine,*map(bits,(1000,-999)))
  e.w(owner+0x54,bits(now),epoch,bits(8))
  before=[bytes(e.uc.mem_read(p,0x100))for p in(source,target)]
  e.call(0x6f058900,a,out);assert e.esp_after==8
  world=e.r(out,2)
  e.call(0x6f05b1c0,b,out+12,out,out+4,0);assert e.esp_after==20
  assert before==[bytes(e.uc.mem_read(p,0x100))for p in(source,target)]
  rows.append(dict(origin=list(map(bits,origin)),source=source_fine,target=target_fine,velocity=velocity,
                   now=bits(now),old=bits(old),epoch=epoch,world=world,distance=e.r(out+12)))
 assert not e.log,'unexpected Storm import'
 return rows

def header(rows):
 s='/* Original058900 ->05b1c0, supplied registry/clock; generated from the frozen Payoff264 oracle. */\nstatic struct {\n    uint32_t origin[2],source[2],target[2],velocity[2],now,old,epoch,distance;\n} const ranking264_distances[]={\n'
 for r in rows:
  s+='    {'+','.join('{'+','.join('0x%08xu'%x for x in r[k])+'}'for k in ['origin','source','target','velocity'])+','+','.join('0x%08xu'%r[k]for k in ['now','old','epoch','distance'])+'},\n'
 return s+'};\n'
