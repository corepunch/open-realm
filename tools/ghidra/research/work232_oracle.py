#!/usr/bin/env python3
"""Execute original center-circle query, including rectangle materialization.

Only Storm storage imports and the caller-supplied visitor are host adapters.
The fixture supplies the game query-context array and canonical object owners.
No original query, numerical, traversal or stopping code is replaced.
"""
import argparse,json,struct,math,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from foot03_rig import Rig

def original(binary):
 from unicorn import UC_HOOK_CODE
 r=Rig(binary,32,32);e=r.e
 e.call(0x6f013490)
 radius_word=e.r(0x6fd6fb38)
 e.w(r.owner+0x234,r.map)
 e.uc.mem_write(r.map+0x64,struct.pack('<2f',8,.125))
 context=r.world.make_query();array=e.fixture(4);e.w(array,context)
 e.w(r.game+0x58,0,1,array,0)
 visitor=e.fixture(16);e.uc.mem_write(visitor,b'\xc3');calls=[];stop_after=[0]
 def callback(uc,address,size,data):
  calls.append(uc.reg_read(e.X.UC_X86_REG_ECX))
  uc.reg_write(e.X.UC_X86_REG_EAX,int(not stop_after[0] or len(calls)<stop_after[0]))
 e.uc.hook_add(UC_HOOK_CODE,callback,begin=visitor,end=visitor)
 rows=[]
 for distance in (999.875,1000,1000.125,1100):
  for stop in (0,1):
   r.reset_map();calls.clear();stop_after[0]=stop
   objects=[]
   for i in range(3):
    mover=r.mover();owner=e.fixture(0x100);unit=e.fixture(0x300)
    e.w(mover+0x30,owner);e.w(owner+0x54,unit)
    obj=r.new_object(mover);objects.append((obj,mover,owner,unit))
   for i,(obj,mover,owner,unit) in enumerate(objects):
    point=(512+(distance if i==0 else 96 if i==1 else 176),512)
    e.uc.mem_write(mover+0x78,struct.pack('<2f',point[0]/32,point[1]/32))
    e.w(obj+0x34,0x010000ca,0);e.w(obj+0x40,0)
    # Original region update publishes each center/radius rectangle into the unit map.
    r.world.update(obj,(math.floor((point[1]-16)/256),math.floor((point[0]-16)/256),
                        math.floor((point[1]+16)/256)+1,math.floor((point[0]+16)/256)+1))
   e.uc.mem_write(r.xy,struct.pack('<2f',512,512));e.w(r.xy+8,radius_word)
   e.call(0x6f05d680,r.xy,r.xy+8,0,0,visitor,0,0,0,edx=r.xy+4)
   if e.esp_after!=32 or e.r(r.game+0x64)!=0 or e.r(context+0x40)!=0:raise ValueError('query scope/ABI differs')
   ids=[next(i for i,o in enumerate(objects)if o[3]==p)for p in calls]
   rows.append(dict(distance=distance,stop_after=stop,candidates=ids,materialized=e.r(context+0x1c),stamp=e.r(r.map+0xb4)))
 return dict(radius_word=radius_word,rows=rows)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 result=original(a.binary);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
