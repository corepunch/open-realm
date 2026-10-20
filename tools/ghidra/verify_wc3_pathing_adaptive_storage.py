#!/usr/bin/env python3
"""Original adaptive storage growth, ushort index alias and public partial/reset results.

Original constructors, growth containers, complete searches and path-owned
wrappers execute unchanged. Only Storm allocation imports receive host backing;
map/owner/scheduler storage is supplied. An explicit enqueue/pop prefix crosses
heap boundaries using nodes from a complete search; it is not a naturally
observed game crowd or a claim about Storm OOM.
"""
import argparse
import ctypes
import hashlib
import json
import struct
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--engine-library', type=Path)
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        u32 = ctypes.POINTER(ctypes.c_uint32)
        engine.pathing_adaptive_route.argtypes = [u32, ctypes.POINTER(ctypes.c_uint8), u32]
        engine.pathing_adaptive_node_state.argtypes = [u32]
        engine.pathing_adaptive_storage.argtypes = [ctypes.c_uint32, u32]
        engine.pathing_adaptive_heap_prefix.argtypes = [ctypes.c_uint32, u32]
        engine.pathing_adaptive_storage(1, (ctypes.c_uint32*3)())
    binary = args.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        parser.error('requires mapped game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + offset)[0] for offset in (28, 56))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (size + 4095) & ~4095)
    uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            uc.mem_write(base + va, binary[offset:offset + count])
    uc.mem_map(0, 4096)
    uc.mem_map(0x10000000, 0xa00000)
    uc.mem_map(0x20000000, 0x10000)
    uc.mem_map(0x40000000, 0x20000000)
    fine, tilemap, cells, bitmap = 0x10000000, 0x10000200, 0x10010000, 0x10001000
    route, route_data, source, target, mask, radius = 0x10000400, 0x10080000, 0x10000500, 0x10000510, 0x10000520, 0x10000530
    stack, stop = 0x20008000, 0x30000000
    allocations, growth = {}, []
    next_block = 0x40000000

    def write(address, *values):
        uc.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, uc.mem_read(address, count * 4)))

    def external(machine, address, size, data):
        nonlocal next_block
        sp = machine.reg_read(UC_X86_REG_ESP)
        count = {0x6f07c6d2: 4, 0x6f07c6d8: 5, 0x6f07c678: 4}[address]
        argv = read(sp + 4, count)
        if address == 0x6f07c678:
            allocations.pop(argv[0])
            result = 1
        else:
            old = argv[0] if address == 0x6f07c6d8 else None
            wanted = argv[1] if old is not None else argv[0]
            result = next_block
            next_block += (wanted + 4095) & ~4095
            if next_block > 0x60000000:
                raise RuntimeError('external storage arena exhausted')
            if old is not None:
                prior = allocations.pop(old)
                machine.mem_write(result, bytes(machine.mem_read(old, min(prior, wanted))))
            allocations[result] = wanted
            wrapper = machine.reg_read(UC_X86_REG_ECX)
            growth.append(dict(wrapper=wrapper-fine, bytes=wanted, previous=0 if old is None else prior))
        machine.reg_write(UC_X86_REG_EAX, result)
        machine.reg_write(UC_X86_REG_ESP, sp + 4 + 4 * count)
        machine.reg_write(UC_X86_REG_EIP, read(sp)[0])

    for address in (0x6f07c6d2, 0x6f07c6d8, 0x6f07c678):
        uc.hook_add(UC_HOOK_CODE, external, begin=address, end=address)

    def run(entry, self, *argv):
        write(stack, stop, *argv)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, self)
        uc.emu_start(entry, stop, count=300000000)
        if uc.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError(f'original operation {entry:x} exceeded instruction bound')
        return uc.reg_read(UC_X86_REG_EAX)

    for address,value in ((0x6fd3c740,-1.0),(0x6fd3c744,0.0),(0x6fd3c748,1.0)):
        uc.mem_write(address,struct.pack('<f',value))
    run(0x6f14f570,fine)
    maps=[0x10001000+0x1000*i for i in range(4)]
    storage=[0x10010000,0x10300000,0x10400000,0x10450000]
    side=512
    for level,(m,data)in enumerate(zip(maps,storage)):
        n=side>>level
        write(fine+0x1c+4*level,m)
        write(m+0x28,data);write(m+0x3c,n,n)
        uc.mem_write(m+0x64,struct.pack('<2f',2<<level,1/(2<<level)))
        if level==0:
            terrain=[(0,0x55000000 if x==384 or (x&1 and y&1)else 0)for y in range(n)for x in range(n)]
            uc.mem_write(data,b''.join(struct.pack('<2I',*v)for v in terrain))
        else:
            for y in range(n):
                for x in range(n):
                    for lane in [0,2,4,6]:run(0x6f15d1c0,0,data+8*(y*n+x),maps[level-1],lane,2*x,2*y)
    route_data=0x10600000
    write(route+0xc,route_data);write(route+0x18,262144,0)
    classes = (ctypes.c_uint8*(sum((512>>l)**2 for l in range(4))))(*[
        (read(data+8*(y*n+x)+4)[0]>>30)&3
        for l,data in enumerate(storage) for n in [512>>l] for y in range(n) for x in range(n)])
    def node_state():
        raw = bytes(uc.mem_read(read(fine+0x5c)[0],36*read(fine+0x6c)[0]))
        return [[n[0],n[1],n[5],n[6],n[2],n[7],
                 0 if n[3]==0xffffffff else 1 if n[3]==0xfffffffe else 2,(n[8]>>16)&255]
                for n in struct.iter_unpack('<9I',raw)]
    def hashes(state):
        fnv = 14695981039346656037
        for byte in b''.join(struct.pack('<8I',*n) for n in state):
            fnv = ((fnv ^ byte) * 1099511628211) & 0xffffffffffffffff
        return dict(node_sha256=hashlib.sha256(json.dumps(state,separators=(',',':')).encode()).hexdigest(),node_fnv64=f'{fnv:016x}')
    def compare(row):
        state = node_state(); row.update(hashes(state))
        if engine:
            words = list(struct.unpack('<4I',struct.pack('<4f',4.25,4.75,448.25,400.75)))
            out=(ctypes.c_uint32*(4+2*65536))()
            engine.pathing_adaptive_route((ctypes.c_uint32*8)(512,512,0,row['budget'],*words),classes,out)
            expected=[row['result'],row['work'],row['nodes'],row['route_count']]+row['route_words']
            if list(out[:len(expected)])!=expected: raise RuntimeError(f"C adaptive route differs at budget{row['budget']}")
            states=(ctypes.c_uint32*(1+8*row['nodes']))();engine.pathing_adaptive_node_state(states)
            if list(states)!=[len(state)]+[w for n in state for w in n]:raise RuntimeError('C final adaptive nodes differ')
            caps=(ctypes.c_uint32*3)();engine.pathing_adaptive_storage(0,caps)
            if list(caps)!=[row['node_capacity'],row['heap_capacity'],row['nodes']]:raise RuntimeError('C retained adaptive capacities differ')
    records=[]
    for budget in [400,2048,5000,10000,40000,65535]:
        uc.mem_write(source,struct.pack('<2f',4.25,4.75));uc.mem_write(target,struct.pack('<2f',448.25,400.75))
        result=run(0x6f162cb0,fine,0,route,source,target,budget,0,0)
        row=dict(budget=budget,result=result,nodes=read(fine+0x6c)[0],work=read(fine+0x9c)[0],node_capacity=read(fine+0x68)[0],heap_capacity=read(fine+0x88)[0],route_count=read(route+0x1c)[0],route_words=read(route_data,2*read(route+0x1c)[0]),nearest=read(fine+0xd0)[0],distance=read(fine+0xcc)[0])
        compare(row);records.append(row);print({k:v for k,v in row.items()if k not in ('route_words','node_sha256','node_fnv64')},flush=True)
    alias=[]
    nodes=read(fine+0x5c)[0]
    for identity in (65535,65536,65537):
        x,y=read(nodes+36*identity,2);level=uc.mem_read(nodes+36*identity+0x22,1)[0]
        cell=storage[level]+8*((y>>level)*(512>>level)+(x>>level))
        found=run(0x6f1625f0,fine,0,x,y)
        alias.append(dict(identity=identity,position=[x,y],level=level,stored_index=read(cell+4)[0]&65535,lookup=found))
        if found != identity&65535:raise RuntimeError('native ushort lookup does not alias')
    path,owner=0x10800000,0x10802000
    write(0x6fd53a48,owner);write(owner+0x250,fine);write(owner+0x538,100)
    write(0x6fd53a74,0xc7fa0001)
    public=[]
    for budget in [400,2048,40000,65535,400]:
        uc.mem_write(path,bytes(0x100))
        uc.mem_write(path+0x1c,struct.pack('<4f',896.5,801.5,896.5,801.5))
        write(path+0x60,route_data);write(path+0x6c,262144,0,-1,-1)
        write(path+0x84,700|(budget<<16));write(path+0x88,0x200000)
        uc.mem_write(path+0xb4,struct.pack('<f',.25))
        for off in [0,0x1c,0x38,0x54]:write(0x6fd53a90+off,65535|1<<16,1000000,0,0,0,0,0)
        uc.mem_write(source,struct.pack('<2f',4.25,4.75));uc.mem_write(target,struct.pack('<2f',448.25,400.75))
        result=run(0x6f166c30,path,source,target,0)
        row=dict(budget=budget,admitted=result,work=read(fine+0x9c)[0],nodes=read(fine+0x6c)[0],count=read(path+0x70)[0],index=read(path+0x78)[0],flags=read(path+0x88)[0],adjusted=read(path+0x24,2),time=read(path+0x80)[0])
        row.update(result=0 if row['flags']&0x20000000 else 1,node_capacity=read(fine+0x68)[0],heap_capacity=read(fine+0x88)[0],route_count=row['count'],route_words=read(route_data,2*row['count']))
        compare(row);public.append(row);print('public',{k:v for k,v in row.items()if k not in ('route_words','node_sha256','node_fnv64')},flush=True)
    heap=[]
    prefix=[]
    initial_slots=read(fine+0x8c)[0]
    for i in range(4097-initial_slots):
        run(0x6f162780,fine,i%read(fine+0x6c)[0])
        slots=read(fine+0x8c)[0]
        if slots in (2048,2049,4096,4097):prefix.append(dict(slots=slots,capacity=read(fine+0x88)[0]))
    while read(fine+0x8c)[0]>1:
        run(0x6f148240,fine+0x70,0x10900000);heap.extend(read(0x10900000,3))
    heap_control=dict(initial_slots=initial_slots,prefix=prefix,words=len(heap),sha256=hashlib.sha256(struct.pack('<'+'I'*len(heap),*heap)).hexdigest(),final_slots=read(fine+0x8c)[0],capacity=read(fine+0x88)[0])
    if engine:
        out=(ctypes.c_uint32*(16+3*4096))();engine.pathing_adaptive_heap_prefix(4096,out)
        if list(out[:1+3*4096])!=[read(fine+0x88)[0]]+heap:raise RuntimeError('C adaptive grown heap pop sequence differs')
    # The next complete public wrapper clears every explicit queued identity and
    # reproduces the first400-budget result with retained grown storage.
    write(path+0x84,700|(400<<16));write(path+0x88,0x200000)
    for off in [0,0x1c,0x38,0x54]:write(0x6fd53a90+off,65535|1<<16,1000000,0,0,0,0,0)
    result=run(0x6f166c30,path,source,target,0)
    recovery=dict(admitted=result,nodes=read(fine+0x6c)[0],work=read(fine+0x9c)[0],route_words=read(route_data,2*read(path+0x70)[0]),index=read(path+0x78)[0],flags=read(path+0x88)[0],heap_capacity=read(fine+0x88)[0])
    if recovery['route_words']!=public[0]['route_words'] or recovery['work']!=public[0]['work']:raise RuntimeError('public recovery differs after heap growth')
    payload=dict(binary_sha256=HASH,scope=__doc__,records=records,public=public,ushort_alias=alias,heap_control=heap_control,recovery=recovery,allocations=growth)
    if args.fixture:
        if args.fixture.exists():
            if json.loads(args.fixture.read_text())!=payload:raise RuntimeError('frozen adaptive storage differs')
        else:args.fixture.write_text(json.dumps(payload,indent=2)+'\n')
    args.report.write_text(json.dumps(dict(passed=True,engine_exact_requests=len(records)+len(public) if engine else 0,**payload),indent=2)+'\n')

if __name__=='__main__':main()
