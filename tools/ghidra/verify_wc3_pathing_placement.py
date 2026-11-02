#!/usr/bin/env python3
"""Execute original public policy2 point-admission searches against production C."""
import struct,json,itertools,argparse,hashlib,ctypes
from pathlib import Path


def main():
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--report',type=Path,required=True);parser.add_argument('--fixture',type=Path);parser.add_argument('--engine-library',type=Path);parser.add_argument('--reference',type=Path);parser.add_argument('--zero-mask-only',action='store_true',help='exercise real queryzero against nonzero terrain flags');args=parser.parse_args()
    binary=args.binary.read_bytes();digest=hashlib.sha256(binary).hexdigest()
    if digest!='d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':parser.error('unsupported game.dll')
    pe=struct.unpack_from('<I',binary,60)[0];opt=pe+24;base,size=(struct.unpack_from('<I',binary,opt+o)[0] for o in (28,56));u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,(size+4095)&~4095)
    for i in range(struct.unpack_from('<H',binary,pe+6)[0]):
     s=opt+struct.unpack_from('<H',binary,pe+20)[0]+40*i;va,n,off=struct.unpack_from('<III',binary,s+12)
     if n:u.mem_write(base+va,binary[off:off+n])
    u.mem_map(0,4096);u.mem_map(0x10000000,0x200000);u.mem_map(0x20000000,0x10000)
    system,tile,cells,mask,radius,point,rect=0x10000000,0x10000200,0x10001000,0x10000800,0x10000810,0x10000820,0x10000830
    stack,stop=0x20008000,0x30000000
    write=lambda p,*v:u.mem_write(p,struct.pack('<'+'I'*len(v),*(x&0xffffffff for x in v)))
    read=lambda p,n=1:list(struct.unpack('<'+'I'*n,u.mem_read(p,4*n)))
    floats=lambda p,*v:u.mem_write(p,struct.pack('<'+'f'*len(v),*v))
    for entry in (0x6f001dd0,0x6f001a80,0x6f001b80):
     write(stack,stop);u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(entry,stop,count=1000)
     if u.reg_read(UC_X86_REG_EIP)!=stop or u.reg_read(UC_X86_REG_ESP)!=stack+4:raise RuntimeError('original scalar startup ABI differs')
    visits=[]
    def hook(machine,address,size,data):
     sp=machine.reg_read(UC_X86_REG_ESP);p,m,cls=read(sp+4,3);visits.append([*read(p,2),cls])
    u.hook_add(UC_HOOK_CODE,hook,begin=0x6f1492b0,end=0x6f1492b0)
    width=height=24
    u.mem_write(cells,struct.pack('<I',0xffffff)*(width*height));write(system+0x1c,tile,1);write(tile+0x28,cells);write(tile+0x3c,width,height);write(tile+0xac,0xffffff)
    engine=ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    records=[]
    for terrain,cls,lane,source,limit,integer in itertools.product(
            ('open','one','square','sealed'),range(4),(0,) if args.zero_mask_only else (0x02000002,0x04000004,0x40000040,0x80000080),
            ((12.25,12.75),(12.125,11.875),(-.125,.25)),(1,5,32),(0,1)):
        blocked={(12,12)} if terrain=='one' else {(x,y) for y in range(10,15) for x in range(10,15)} if terrain=='square' else {(x,y) for y in range(height) for x in range(width)} if terrain=='sealed' else set()
        data=bytes((2 if lane==0 else lane>>24) if (x,y) in blocked else 0 for y in range(height) for x in range(width))
        u.mem_write(cells,b''.join(struct.pack('<I',(v<<24)|0xffffff) for v in data))
        outputs=[];chains=[]
        for mode in (0,7):
            visits.clear();floats(point,*source);floats(rect,source[1],source[0],source[1],source[0]);floats(radius,cls*.5);write(mask,lane);write(system+0xd4,mode)
            write(stack,stop,point,rect,2,radius,mask,limit,0,0,integer);u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,system);u.emu_start(0x6f14a1e0,stop,count=20000000)
            if u.reg_read(UC_X86_REG_EIP)!=stop or u.reg_read(UC_X86_REG_ESP)!=stack+40 or read(system+0xd4)!=[mode] or read(0)!=[0]:raise RuntimeError('placement ABI/mode/SEH restoration differs')
            outputs.append([u.reg_read(UC_X86_REG_EAX),*read(point,2)]);chains.append(list(visits))
        if outputs[0]!=outputs[1] or chains[0]!=chains[1]:raise RuntimeError('placement repeat differs')
        inp=[width,height,*struct.unpack('<II',struct.pack('<ff',*source)),limit,cls,lane,integer]
        record=dict(terrain=terrain,input=inp,output=outputs[0],visits=len(chains[0]),visit_sha256=hashlib.sha256(json.dumps(chains[0],separators=(',',':')).encode()).hexdigest())
        if args.zero_mask_only:record['terrain_mask']=2
        records.append(record)
        if engine:
            actual=(ctypes.c_uint32*3)();engine.pathing_fine_placement((ctypes.c_uint32*8)(*inp),(ctypes.c_uint8*len(data)).from_buffer_copy(data),actual)
            if list(actual)!=outputs[0]:raise RuntimeError(str(record|{'engine':list(actual)}))
    result=dict(passed=True,binary_sha256=digest,cases=len(records),original_calls=2*len(records),stack_abi_verified=True,mode_seh_restoration=True,
                scope=('Complete original14a1e0 policy2 real zero-mask queries against authored walk blockage across four classes, integer/centre output and bounded budgets; callback null.' if args.zero_mask_only else 'Complete original14a1e0 policy2 degenerate-point admission, four classes/masks, integer/centre output and bounded budgets; callback null. Public callback/bridge/bounds admission remains separate.'))
    if args.reference and json.loads(args.reference.read_text())!=result|{'cases':records}:raise RuntimeError('frozen original placement reference differs')
    if args.fixture:args.fixture.write_text(json.dumps(result|{'cases':records},indent=2)+'\n')
    args.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
