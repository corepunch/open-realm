#!/usr/bin/env python3
"""Execute original widget extent/collection-bounds producers without stubs."""
import argparse, hashlib, itertools, json, struct
from pathlib import Path
SHA='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
def original(binary):
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
 from unicorn import x86_const as X
 raw=Path(binary).read_bytes()
 if hashlib.sha256(raw).hexdigest()!=SHA:raise ValueError('wrong game.dll')
 pe=struct.unpack_from('<I',raw,60)[0];opt=pe+24
 base,size=[struct.unpack_from('<I',raw,opt+n)[0]for n in (28,56)]
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
 for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
  s=opt+struct.unpack_from('<H',raw,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',raw,s+12)
  if n:u.mem_write(base+va,raw[off:off+n])
 u.mem_map(0x10000000,0x100000)
 stop,stack,owner,fine,collection,array,texture,center,bounds=range(0x10000000,0x10000900,0x100)
 objects=[0x10001000+i*0x100 for i in range(4)]
 def words(a,v):u.mem_write(a,struct.pack('<'+'I'*len(v),*[x&0xffffffff for x in v]))
 def floats(a,v):u.mem_write(a,struct.pack('<'+'f'*len(v),*v))
 def call(entry,ecx,args=(),ret=0,edx=0):
  words(stack,[stop,*args]);u.reg_write(X.UC_X86_REG_ESP,stack)
  u.reg_write(X.UC_X86_REG_ECX,ecx);u.reg_write(X.UC_X86_REG_EDX,edx)
  u.emu_start(entry,stop,count=10000)
  if u.reg_read(X.UC_X86_REG_EIP)!=stop or u.reg_read(X.UC_X86_REG_ESP)!=stack+4+ret:raise ValueError('original ABI/budget differs')
 words(0x6fd3c82c,[owner])
 for entry in (0x6f001dd0,0x6f001a80,0x6f001b80):call(entry,0)
 call(0x6f070d80,0x6fd68b74,edx=16)
 words(collection,[4,4,array]);words(array,objects)
 for obj in objects:words(obj+0x2c,[fine])
 cases=[]
 for dimensions,turn,origin,point in itertools.product([(1,1),(2,3),(8,8),(9,4)],range(4),[(0,0),(-1024,-512)],[(1024,1024),(0,0),(-.125,16.125)]):
  floats(owner+0x6c,origin);floats(center,point);words(texture+8,dimensions)
  call(0x6f22f1d0,texture,[bounds,center,turn],12)
  for obj in objects:words(obj+0x1c,[-1]*4)
  call(0x6f0642f0,collection,[bounds],4)
  boxes=[list(struct.unpack('<4i',u.mem_read(obj+0x1c,16)))for obj in objects]
  if any(b!=boxes[0]for b in boxes):raise ValueError('collection extents differ')
  cases.append(dict(dimensions=list(dimensions),turn=turn,origin=list(origin),point=list(point),
   world_bounds=list(struct.unpack('<4I',u.mem_read(bounds,16))),box=boxes[0]))
 return dict(binary_sha256=SHA,cases=cases)
def header(spec):
 out=['#ifndef RETAIL_REGION_BOUNDS230_H','#define RETAIL_REGION_BOUNDS230_H','/* Literal original22f1d0/0642f0 outputs, Work230; Y,X rectangle order. */',
 'static struct {unsigned width,height,turn;float origin[2],point[2];int box[4];} const region_bounds230[]={']
 for r in spec['cases']:
  v=lambda a:'{'+','.join(str(x)for x in a)+'}'
  out.append('{'+','.join([*map(str,r['dimensions']),str(r['turn']),v(r['origin']),v(r['point']),v(r['box'])])+'},')
 out.append('};')
 if 'scopes' in spec:
  out.append('static struct {uint32_t result,alias_result;uint8_t before[1360],after[1360];} const region_scope230[]={')
  for row in spec['scopes']:
   v=lambda a:'{'+','.join(str(x)for x in a)+'}'
   out.append('{'+','.join([str(row['result'])+'u',str(row.get('alias_result',0xffffffff))+'u',v(row['before']),v(row['after'])])+'},')
  out.append('};')
 return '\n'.join(out+['#endif',''])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--header',type=Path);a=p.parse_args()
 if a.report.exists():p.error('new report required')
 r=original(a.binary);a.report.write_text(json.dumps(r,indent=1)+'\n')
 if a.header:a.header.write_text(header(r))
 print('original region bounds',len(r['cases']))
if __name__=='__main__':main()
