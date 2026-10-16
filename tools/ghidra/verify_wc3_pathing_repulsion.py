#!/usr/bin/env python3
"""Original repulsion settings, category producer and ordered arithmetic slices, with exact production C words."""
import argparse, ctypes, hashlib, itertools, json, math, struct
from pathlib import Path
from wc3_shipped_crt import load_crt
from verify_wc3_pathing_numeric import initialize_runtime_scalars


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EBP
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--fixture',type=Path);p.add_argument('--engine-library',type=Path);a=p.parse_args()
    b=a.binary.read_bytes();sha=hashlib.sha256(b).hexdigest()
    if sha!='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':p.error('unsupported DLL')
    pe=struct.unpack_from('<I',b,60)[0];opt=pe+24;base,size=[struct.unpack_from('<I',b,opt+n)[0] for n in (28,56)]
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
    for i in range(struct.unpack_from('<H',b,pe+6)[0]):
        s=opt+struct.unpack_from('<H',b,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',b,s+12)
        if n:u.mem_write(base+va,b[off:off+n])
    u.mem_map(0x10000000,0x20000);u.mem_map(0x20000000,0x10000)
    system,stack,stop=0x10000000,0x20008000,0x30000000
    def write(at,*words):u.mem_write(at,struct.pack('<'+'I'*len(words),*(w&0xffffffff for w in words)))
    def read(at,n):return list(struct.unpack('<'+'I'*n,u.mem_read(at,n*4)))
    def floats(at,*values):u.mem_write(at,struct.pack('<'+'f'*len(values),*values))
    def run(entry,ecx=0,*args):
        write(stack,stop,*args);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,ecx)
        u.emu_start(entry,stop,count=1000000)
        if u.reg_read(UC_X86_REG_EIP)!=stop:raise ValueError('original routine exceeded budget')
        return u.reg_read(UC_X86_REG_EAX)
    initialize_runtime_scalars(u,stack,stop)
    crt=load_crt(u,a.binary.parent/'msvcr120.dll');write(0x6fa7c4fc,crt['exports']['isdigit'])
    run(0x6f004790)
    settings=[read(0x6fd54398+i*20,5) for i in range(16)]
    # Complete695090 uses actual CUnit vtableEC685da0 and the original rawcode cache lookup.
    unit,keyptr,bucket,record=[system+n for n in (0x100,0x1000,0x1100,0x1200)]
    key=0x6f6f6668;write(keyptr,key);codehash=run(0x6f198420,keyptr)
    write(bucket,0,0,record);write(record,codehash);write(record+0x14,key)
    write(0x6fd709f4,bucket);write(0x6fd709fc,0);write(unit,0x6fb77eb0);write(unit+0x30,key)
    categories=[]
    for owner,flag,group in itertools.product(range(16),(0,1),range(32)):
        write(unit+0x58,owner);write(unit+0x60,flag);write(record+0x22c,group)
        result=run(0x6f695090,unit)
        assert result==(((15 if flag else owner)&15)<<4)|(group&15)
        categories.append([owner,flag,group,result])
    engine=ctypes.CDLL(str(a.engine_library.resolve())) if a.engine_library else None
    separate,config,owner=system+0x3000,system+0x4000,system+0x5000;frame=stack-0x1000
    write(0x6fd53a48,owner);pairs=[];tails=[]
    distances=(0,.0005,.001,.01,.5,1,2,4.9,5,6.9,7,8,9,10,11)
    for row,dist,angle,prior in itertools.product(range(5),distances,(0,.7,1.6,-2.3),((0,0),(.1,-.2))):
        seed=(row*31+len(pairs))*0x10203041&0xffffffff;write(owner,seed,0)
        write(config,*settings[row]);floats(separate+0x18,*prior)
        floats(frame-0x68,dist*math.cos(angle),dist*math.sin(angle));floats(frame-0x60,0,0)
        inputs=read(owner,2)+read(config,5)+read(frame-0x68,2)+read(frame-0x60,2)+read(separate+0x18,2)
        write(frame-8,config);u.reg_write(UC_X86_REG_EBP,frame);u.reg_write(UC_X86_REG_ESP,frame-0x100)
        u.reg_write(UC_X86_REG_EBX,separate);u.reg_write(UC_X86_REG_ESI,config)
        u.emu_start(0x6f170359,0x6f170367,count=10000);u.emu_start(0x6f1703e0,0x6f170518,count=10000)
        assert u.reg_read(UC_X86_REG_EIP)==0x6f170518
        output=read(owner,2)+read(separate+0x18,2);pairs.append(dict(input=inputs,output=output))
        if engine:
            actual=(ctypes.c_uint32*4)();engine.pathing_repulsion_pair((ctypes.c_uint32*13)(*inputs),actual)
            assert list(actual)==output,('pair',inputs,output,list(actual))
    seeded=json.loads(Path(__file__).with_name('fixtures').joinpath('retail-pathfinding-random-1.27.json').read_text())
    initial=next(sequence['initial'] for sequence in seeded['sequences'] if sequence['seed']==12345)
    write(owner,*initial);write(config,*settings[0]);floats(separate+0x18,0,0)
    floats(frame-0x68,16,16);floats(frame-0x60,16,16);write(frame-8,config)
    u.reg_write(UC_X86_REG_EBP,frame);u.reg_write(UC_X86_REG_ESP,frame-0x100)
    u.reg_write(UC_X86_REG_EBX,separate);u.reg_write(UC_X86_REG_ESI,config)
    u.emu_start(0x6f170359,0x6f170367,count=10000);u.emu_start(0x6f1703e0,0x6f170518,count=10000)
    first_random=read(owner,2);first_pair=read(separate+0x18,2);write(separate+0x20,0)
    write(frame,0,stop);u.reg_write(UC_X86_REG_ESP,frame-0x7c)
    u.emu_start(0x6f170525,stop,count=10000)
    first_overlap=dict(seed=12345,initial=initial,source=[0x41800000,0x41800000],
        pair=first_pair,tail=read(separate+0x18,3),random=first_random)
    for row,mag,angle in itertools.product(range(5),(0,.005,.02,.1,.5,1,10),(0,.7,-2.3)):
        write(config,*settings[row]);floats(separate+0x18,mag*math.cos(angle),mag*math.sin(angle));write(separate+0x20,0xabcd0000)
        inputs=read(separate+0x18,2)+settings[row]+[0xabcd0000]
        write(frame,0,stop);u.reg_write(UC_X86_REG_EBP,frame);u.reg_write(UC_X86_REG_ESP,frame-0x7c)
        u.reg_write(UC_X86_REG_EBX,separate);u.reg_write(UC_X86_REG_ESI,config)
        u.emu_start(0x6f170525,stop,count=10000);assert u.reg_read(UC_X86_REG_EIP)==stop
        output=read(separate+0x18,3);tails.append(dict(input=inputs,output=output))
        if engine:
            actual=(ctypes.c_uint32*3)();engine.pathing_repulsion_tail((ctypes.c_uint32*8)(*inputs),actual)
            assert list(actual)==output,('tail',inputs,output,list(actual))
    result=dict(passed=True,binary_sha256=sha,crt_sha256=crt['sha256'],settings_rows=16,category_cases=len(categories),pair_cases=len(pairs),tail_cases=len(tails),engine_exact_cases=len(pairs)+len(tails) if engine else 0,scope='Full original004790 settings initializer/default shipped CRT isdigit; full695090 category producer with authentic CUnit vtable and supplied cache. Pair170359..367 +1703e0..518 and tail170525..RET slices; query/application/scheduling excluded.')
    if a.fixture:a.fixture.write_text(json.dumps(result|dict(settings=settings,categories=categories,pairs=pairs,tails=tails,first_overlap=first_overlap),separators=(',',':'))+'\n')
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
