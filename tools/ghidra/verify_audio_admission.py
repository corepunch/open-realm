"""Execute retail admission machine code and compare with production C.
Local only: requires the user's matching game.dll, never distributes its bytes.
Hash/list query shims replace dependencies, not admission decisions or ordering.
"""
import argparse, hashlib, json, random, struct, subprocess
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--probe', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
parser.add_argument('--cases', type=int, default=12000)
parser.add_argument('--seed', type=int, default=20260925)
args=parser.parse_args()
binary=args.binary.read_bytes()
binary_hash=hashlib.sha256(binary).hexdigest()
if binary_hash!='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
 parser.error('wrong retail binary: addresses require game.dll 1.27.1.7085')
if args.cases <= 0: parser.error('--cases must be positive')
u16=lambda p:struct.unpack_from('<H',binary,p)[0]
u32=lambda p:struct.unpack_from('<I',binary,p)[0]
pe=u32(0x3c); opt=pe+24; base=u32(opt+28); size=u32(opt+56)
u=Uc(UC_ARCH_X86,UC_MODE_32); u.mem_map(base,(size+4095)&~4095)
u.mem_write(base,binary[:u32(opt+60)])
for i in range(u16(pe+6)):
 p=opt+u16(pe+20)+40*i
 va,rawsize,raw=struct.unpack_from('<III',binary,p+12)
 if rawsize:u.mem_write(base+va,binary[raw:raw+rawsize])
u.mem_map(0x10000000,0x30000);u.mem_map(0x11000000,0x1000);u.mem_map(0x20000000,0x10000)
def w(p,v):u.mem_write(p,struct.pack('<I',v&0xffffffff))
def r(p):return struct.unpack('<I',u.mem_read(p,4))[0]
def cstr(p):return bytes(u.mem_read(p,260)).split(b'\0',1)[0]
def ret(value=0,pop=0):
 sp=u.reg_read(UC_X86_REG_ESP);target=r(sp)
 u.reg_write(UC_X86_REG_EAX,value);u.reg_write(UC_X86_REG_ESP,sp+4+pop);u.reg_write(UC_X86_REG_EIP,target)
state=[];victims=set()
addr=lambda i:0x10000000+i*0x300
tracked=lambda x:not(x[2]&0x8000)
def shim(u,a,size,ctx):
 ecx=u.reg_read(UC_X86_REG_ECX);sp=u.reg_read(UC_X86_REG_ESP)
 if a==0x6f0b2b60: ret(int(any(x[3]==ecx and tracked(x) for x in state)))
 elif a==0x6f0af490:
  victims.update(i for i,x in enumerate(state) if x[3]==ecx and tracked(x));ret()
 elif a==0x6f07c72c: ret(0 if cstr(r(sp+4))==cstr(r(sp+8)) else 1,12)
 elif a==0x6f0abee0: ret(sum(cstr(addr(i)+0x34)==cstr(ecx) for i in range(len(state))))
 elif a==0x6f0abcf0: ret(pop=4) # callback side effects are outside admission
 elif a==0x6f0b2230: victims.add((ecx-0x10000000)//0x300);ret()
 elif a==0x11000080:ret() # variadic logger; caller clears arguments
for a in [0x6f0b2b60,0x6f0af490,0x6f07c72c,0x6f0abee0,0x6f0abcf0,0x6f0b2230,0x11000080]:
 u.hook_add(UC_HOOK_CODE,shim,begin=a,end=a)
w(0x11000000,0x11000100);w(0x1100010c,0x11000080)
def oracle(req,rows,limit):
 global state,victims
 state=rows;victims=set();n=len(rows)
 u.mem_write(0x10000000,b'\0'*0x22000)
 for i,x in enumerate(rows+[req+[0,0]]):
  f,pr,fl,us,gr,tick,serial=x;p=addr(i)
  u.mem_write(p+0x34,('voice%d.wav'%f).encode()+b'\0')
  for off,val in [(0x13c,fl),(0x140,pr),(0x148,us),(0x178,gr),(0x17c,tick)]:w(p+off,val)
 order=sorted(range(n),key=lambda i:rows[i][6])
 for k,i in enumerate(order):w(addr(i)+12,addr(order[k+1]) if k+1<n else 0)
 w(0x6fcd59d4,8);w(0x6fcd59dc,addr(order[0]) if n else 0)
 w(0x6fd461dc,n);w(0x6fd46154,16);w(0x6fd46158,0x10020000);w(0x6fd3cf10,0x10021000)
 for g in range(16):
  seq=sorted([i for i in range(n) if rows[i][4]==g],key=lambda i:(rows[i][1],-rows[i][6]))
  h=0x10020000+12*g;w(h,16);w(h+8,addr(seq[0]) if seq else 0)
  for k,i in enumerate(seq):w(addr(i)+20,addr(seq[k+1]) if k+1<len(seq) else 0)
  w(0x10021000+20*g,limit)
 sp=0x20008000;w(sp,0x30000000);w(sp+4,0x11000000)
 u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,addr(n))
 u.emu_start(0x6f0af5e0,0x30000000,count=10000)
 assert u.reg_read(UC_X86_REG_EIP)==0x30000000,hex(u.reg_read(UC_X86_REG_EIP))
 return int(u.reg_read(UC_X86_REG_EAX)==0),sum(1<<i for i in victims)
rng=random.Random(args.seed);cases=[];expected=[];inputs=[]
flags=[4,8,16,32,64,128,1024,2048,32768]
priorities=[0,1,99,100,1000,0x80000000,0xffffffff]
for k in range(args.cases):
 n=rng.choice([0,1,2,3,4,8,16,23,24]);limit=rng.choice([1,2,3,4,8,16,24])
 req=[rng.randrange(5),rng.choice(priorities),sum(f for f in flags if rng.randrange(2)),rng.randrange(5),rng.randrange(4)]
 order=rng.sample(range(1,n+1),n)
 rows=[[rng.randrange(5),rng.choice(priorities),rng.choice([0,32768]),rng.randrange(5),rng.randrange(4),order[i]//3,order[i]] for i in range(n)]
 # Also exercise timestamps independent of creation order.
 if k%4==0:
  for x in rows:x[5]=rng.randrange(4)
 cases.append((req,limit,rows));expected.append(oracle(req,rows,limit))
 inputs.append(' '.join(map(str,[n]+req+[limit]))+'\n'+'\n'.join(' '.join(map(str,x)) for x in rows)+'\n')
p=subprocess.run([str(args.probe.resolve())],input=''.join(inputs),text=True,capture_output=True,check=True)
actual=[tuple(map(int,line.split())) for line in p.stdout.splitlines()]
assert len(actual)==len(expected)
failures=[{'case':i,'request':cases[i][0],'channel_limit':cases[i][1],'active':cases[i][2],'retail':e,'openrealm':a} for i,(e,a) in enumerate(zip(expected,actual)) if e!=a]
args.report.write_text(json.dumps({'binary_sha256':binary_hash,'probe_sha256':hashlib.sha256(args.probe.read_bytes()).hexdigest(),'seed':args.seed,'cases':len(cases),'mismatches':failures},indent=2))
print(f'{len(cases)} retail x86 / C comparisons; {len(failures)} mismatches')
if failures:print(json.dumps(failures[:2],indent=2));raise SystemExit(1)
