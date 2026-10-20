#!/usr/bin/env python3
"""Original owner PRNG, seeded public queries and overlap direction words; seed secondary streams excluded."""
import argparse, ctypes, hashlib, json, struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--fixture',type=Path);p.add_argument('--engine-library',type=Path);args=p.parse_args()
    b=args.binary.read_bytes();digest=hashlib.sha256(b).hexdigest()
    if digest!='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':p.error('unsupported game.dll')
    pe=struct.unpack_from('<I',b,60)[0];opt=pe+24;base,size=(struct.unpack_from('<I',b,opt+i)[0] for i in (28,56))
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
    for i in range(struct.unpack_from('<H',b,pe+6)[0]):
        s=opt+struct.unpack_from('<H',b,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',b,s+12)
        if n:u.mem_write(base+va,b[off:off+n])
    u.mem_map(0,4096);u.mem_map(0x10000000,0x100000);u.mem_map(0x20000000,0x10000)
    owner,x,y,out=0x10000000,0x10001000,0x10001010,0x10001020;stack,stop=0x20008000,0x30000000
    def write(at,*v):u.mem_write(at,struct.pack('<'+'I'*len(v),*(q&0xffffffff for q in v)))
    def read(at,n=2):return list(struct.unpack('<'+'I'*n,u.mem_read(at,n*4)))
    def run(entry,*v,ecx=0,edx=0,purge=0):
        write(stack,stop,*v);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        u.emu_start(entry,stop,count=2000000)
        if u.reg_read(UC_X86_REG_EIP)!=stop or u.reg_read(UC_X86_REG_ESP)!=stack+4+purge:raise RuntimeError('original random ABI differs')
        return u.reg_read(UC_X86_REG_EAX)
    for entry in (0x6f001dd0,0x6f001a80,0x6f001b80):run(entry)
    write(0x6fd53a48,owner)
    table=read(0x6fa92f10,61)
    engine=ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    seeds=(0,1,47,53,59,61,7085,12345,0x7fffffff,0x80000000,0xffffffff)
    integer_bounds=((0,0),(7,7),(1,10),(-10,-1),(-9,13),(12,-4),(-2147483648,2147483647),
                    (2147483647,-2147483648),(0,2147483647),(-2147483648,0),(0,-1),(2147483646,2147483647))
    real_bounds=((0.,0.),(7.,7.),(0.,1.),(-10.,-1.),(-9.,13.),(12.,-4.),
                 (0.,2**-22),(0.,2**-23),(-.125,.875),(-512.,512.),(2**-10,2**-9),(2.,-2.))
    sequences=[];calls=0
    for seed in seeds:
        # Execute214140 through its actual seed/one-draw prefix, stopping before
        #693710's separate45 unit streams/TLS reseed. No platform import is stubbed.
        write(stack,stop,seed);u.reg_write(UC_X86_REG_ESP,stack)
        u.emu_start(0x6f214140,0x6f693710,count=10000)
        initial=read(owner)
        if u.reg_read(UC_X86_REG_EIP)!=0x6f693710 or u.reg_read(UC_X86_REG_ESP)!=stack or u.reg_read(UC_X86_REG_ECX)!=initial[0]:
            raise RuntimeError('public seed prefix/tail arguments differ')
        state=(ctypes.c_uint32*2)()
        if engine:
            engine.pathing_random_seed(ctypes.c_uint32(seed),state)
            if list(state)!=initial:raise RuntimeError('engine seeded owner state differs')
        operations=[]
        for k in range(128):
            kind=k%4
            if kind==0:inp=[];result=[run(0x6f1b7130,ecx=owner)]
            elif kind==1:
                inp=[v&0xffffffff for v in integer_bounds[(k//4)%len(integer_bounds)]]
                result=[run(0x6f201e30,*inp)]
            elif kind==2:
                inp=list(struct.unpack('<II',struct.pack('<ff',*real_bounds[(k//4)%len(real_bounds)])))
                write(x,inp[0]);write(y,inp[1]);result=[run(0x6f201e70,x,y)]
            else:
                inp=[];write(out,0xdeadbeef,0xdeadbeef)
                returned=run(0x6f1d19e0,ecx=out,edx=owner)
                if returned!=out:raise RuntimeError('direction pointer return differs')
                result=read(out)
            calls+=1;actual_state=read(owner);operations.append(dict(kind=kind,input=inp,output=result,state=actual_state))
            if engine:
                output=(ctypes.c_uint32*2)();inputs=(ctypes.c_uint32*2)(*(inp or [0,0]))
                engine.pathing_random_query((ctypes.c_uint32*3)(kind,*inputs),state,output)
                if list(output)[:len(result)]!=result or list(state)!=actual_state:raise RuntimeError(str(operations[-1]|{'engine':list(output),'engine_state':list(state)}))
        sequences.append(dict(seed=seed,initial=initial,operations=operations))
    result=dict(passed=True,binary_sha256=digest,seeds=len(seeds),original_calls=calls,seed_prefixes=len(seeds),
                stack_abi_verified=True,scope='Full original PRNG/public GetRandomInt/GetRandomReal/overlap direction calls;214140 seed+one-draw prefix. Separate693710 per-unit/TLS reseed and owner startup consumers excluded.')
    if args.fixture:args.fixture.write_text(json.dumps(result|dict(table_words=table,sequences=sequences),separators=(',',':'))+'\n')
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
