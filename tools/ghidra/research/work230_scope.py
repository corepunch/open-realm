#!/usr/bin/env python3
"""Execute complete original059590 widget distance scopes without stubs."""
import hashlib,struct
from pathlib import Path
from verify_wc3_pathing_numeric import initialize_runtime_scalars
from research.work230_oracle import SHA

def original(binary):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn import x86_const as X
 raw=Path(binary).read_bytes()
 if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('wrong game.dll')
 pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24
 base,size=[struct.unpack_from('<I',raw,opt+n)[0]for n in (28,56)]
 m=Uc(UC_ARCH_X86,UC_MODE_32);m.mem_map(base,(size+4095)&~4095)
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  s=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',raw,s+12)
  if n:m.mem_write(base+va,raw[off:off+n])
 m.mem_map(0x10000000,0x1000000)
 stack,stop=0x10f80000,0x10ff0000
 owner,system,tilemap,acc=0x10000000,0x10001000,0x10002000,0x10003000
 source,goal,collection,array,widgetlist,obj=0x10004000,0x10004100,0x10004200,0x10004300,0x10004400,0x10004500
 maps=[0x10005000+i*0x100 for i in range(4)];storage=[0x10100000+i*0x10000 for i in range(4)]
 cells,links,bitmap,nodes,heap=0x10200000,0x10300000,0x10400000,0x10500000,0x10600000
 def w(a,*v):m.mem_write(a,struct.pack('<'+'I'*len(v),*[x&0xffffffff for x in v]))
 def f(a,*v):m.mem_write(a,struct.pack('<'+'f'*len(v),*v))
 def run(entry,ecx=0,args=(),edx=0,ret=0):
  w(stack,stop,*args);m.reg_write(X.UC_X86_REG_ESP,stack);m.reg_write(X.UC_X86_REG_ECX,ecx);m.reg_write(X.UC_X86_REG_EDX,edx)
  m.emu_start(entry,stop,count=10000000)
  if m.reg_read(X.UC_X86_REG_EIP)!=stop or m.reg_read(X.UC_X86_REG_ESP)!=stack+4+ret:raise ValueError('original scope ABI/budget differs')
  return m.reg_read(X.UC_X86_REG_EAX)
 def classes():
  return [m.mem_read(storage[lev]+i*8+7,1)[0]for lev in range(4)for i in range((32>>lev)**2)]
 active=False;events=[]
 def observe(u,address,size,data):
  if active and address==0x6f15d360:
   sp=m.reg_read(X.UC_X86_REG_ESP);p,clear=struct.unpack('<II',m.mem_read(sp+4,8))
   events.append(dict(box=list(struct.unpack('<4i',m.mem_read(p,16))),clear=clear))
 m.hook_add(UC_HOOK_CODE,observe,begin=0x6f15d360,end=0x6f15d360)
 initialize_runtime_scalars(m,stack,stop);m.mem_write(0x6fd53a74,struct.pack('<f',-128000.0078125));run(0x6f0040d0)
 w(0x6fd53a48,owner);w(0x6fd3c82c,owner);w(owner+0x23c,*maps);w(owner+0x24c,system,acc)
 w(system+0x1c,tilemap,1);w(tilemap+0x28,cells);w(tilemap+0x3c,64,64);w(tilemap+0x54,0,0,64,64)
 w(tilemap+0x78,links);w(tilemap+0x84,8192,64);w(tilemap+0x98,bitmap);w(tilemap+0xac,0xffffff)
 w(obj+0x1c,28,28,37,37);w(obj+0x34,0x010000c2,0);w(obj+0x40,0x10000000)
 w(collection,1,1,array);w(array,obj)
 out=[]
 for mode in range(8):
  words=[0xffffff]*4096
  if mode&4:
   for y in range(64):words[y*64+20]|=2<<24
  for n,(x,y)in enumerate((x,y)for y in range(28,36)for x in range(28,36)):
   words[y*64+x]=n;w(links+n*8,0x01ffffff,obj)
  w(cells,*words);m.mem_write(bitmap,bytes(512));m.mem_write(acc,bytes(0x400))
  for lev,(tm,store)in enumerate(zip(maps,storage)):
   side=32>>lev;m.mem_write(tm,bytes(0x100));m.mem_write(store,bytes(side*side*8))
   w(tm+0x28,store);w(tm+0x3c,side,side);f(tm+0x64,2<<lev,1/(2<<lev));w(acc+0x1c+4*lev,tm)
  w(acc+0x5c,nodes);w(acc+0x68,4096,0);w(acc+0x7c,heap);w(acc+0x88,65536,0)
  run(0x6f15d360,owner,(0,0),ret=8)
  before=classes()
  w(cells+(37*64+37)*4,0x02ffffff);w(cells+(47*64+47)*4,0x02ffffff)
  f(source,*( (1024,1024)if mode&1 else(256,1024)));f(goal,*((256,1024)if mode&1 else(1024,1024)))
  w(widgetlist,*((collection,0)if mode&1 else(0,collection)))
  events=[];active=True
  result=run(0x6f059590,source,(6,5000,0,0,widgetlist,2,0,0),edx=goal,ret=32)
  active=False
  after=classes()
  row=dict(mode=mode,result=result,events=list(events),before=before,after=after)
  if mode&2:
   f(source,1024,1024);f(goal,1024,1024);w(widgetlist,collection,collection);events=[];active=True
   row['alias_result']=run(0x6f059590,source,(6,5000,0,0,widgetlist,2,0,0),edx=goal,ret=32);active=False;row['alias_events']=list(events)
  out.append(row)
 return out
