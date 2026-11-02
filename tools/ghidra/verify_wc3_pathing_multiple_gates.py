#!/usr/bin/env python3
"""Original special-edge route, semantic node and distance controls.

Runs unchanged 162cb0/1627e0 and source rectangle publication over supplied
empty fine storage and padded map headers. No hooks or physical placement
stand-ins in these 512 requests: chained and equal-distance alternatives,
both publication orders and all activation combinations. Public traversal
is a separate oracle.
"""
import argparse
import collections
import ctypes
import hashlib
import json
import struct
from pathlib import Path



def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--fixture',type=Path)
    parser.add_argument('--engine-library',type=Path)
    args=parser.parse_args()
    engine=ctypes.CDLL(str(args.engine_library.resolve()))if args.engine_library else None
    exact_publications=exact_requests=0
    binary = args.binary.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        parser.error('unsupported binary; requires game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + offset)[0] for offset in (28, 56))
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (size + 4095) & ~4095)
    machine.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            machine.mem_write(base + va, binary[offset:offset + count])

    machine.mem_map(0x10000000, 0x400000)
    machine.mem_map(0x20000000, 0x10000)
    system, maps = 0x10000000, [0x10001000 + i * 0x100 for i in range(4)]
    data = [0x10010000 + i * 0x10000 for i in range(4)]
    nodes, heap, route, route_data = 0x10080000, 0x10100000, 0x10200000, 0x10201000
    source_ptr, target_ptr = 0x10000400, 0x10000410
    stack, stop, width = 0x20008000, 0x30000000, 32
    start, goal = (4, 4), (27, 27)
    source, target = (6.25, 6.75), (27.25, 27.75)
    constants = {'6fd3c740': -1.0, '6fd3c744': 0.0, '6fd3c748': 1.0, '6fd53a74': -128000.0078125}
    for address, value in constants.items():
        machine.mem_write(int(address, 16), struct.pack('<f', value))

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, machine.mem_read(address, count * 4)))

    def run(entry, self, *arguments):
        write(stack, stop, *arguments)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, self)
        machine.emu_start(entry, stop, count=20000000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail adaptive operation exceeded instruction budget')
        return machine.reg_read(UC_X86_REG_EAX)

    owner,fine_system,fine,game,cells,rectangle,special=0x10300000,0x10301000,0x10302000,0x10303000,0x10310000,0x10308000,0x10309000
    write(0x6fd53a48,owner);write(0x6fd3c82c,game)
    write(game+0x6c,0,0)
    write(owner+0x24c,fine_system);write(fine_system+0x1c,fine)
    write(owner+0x23c,*maps);write(owner+0x250,system)
    write(fine+0x28,cells);write(fine+0x3c,64,64);write(fine+0x54,0,0,64,64)
    write(system+0x3c,special)
    for level,(tilemap,storage)in enumerate(zip(maps,data)):
        side=41>>level
        write(tilemap+0x28,storage);write(tilemap+0x3c,side,side);write(system+0x1c+level*4,tilemap)
        machine.mem_write(tilemap+0x64,struct.pack('<ff',2<<level,1/(2<<level)))
    write(system+0x5c,nodes);write(system+0x68,4096,0)
    write(system+0x7c,heap);write(system+0x88,65536,0)
    write(route+0xc,route_data);write(route+0x18,4096,0)
    write(cells,*([0xffffff]*64*64))
    run(0x6f15d360,owner,0,0)
    initial_classes=[read(storage+8*i+4)[0]>>24 for level,storage in enumerate(data)for i in range((41>>level)**2)]
    observations=[]
    A=[312.,312.,712.,712.];B=[568.,568.,968.,968.]
    def state_bytes(classbytes,markers):
        state=[];off=0
        for side in (41,20,10,5):
            n=side*side
            for lane in (0,2,4,6):state.extend((v>>(6-lane))&3 for v in classbytes[off:off+n])
            off+=n
        return state+markers
    u32=ctypes.POINTER(ctypes.c_uint32);u8=ctypes.POINTER(ctypes.c_uint8)
    if engine:
        engine.pathing_waygate_publish.argtypes=[u32,ctypes.c_uint32,u8]
        engine.pathing_adaptive_special_route.argtypes=[u32,u8,u8,u32,u32]
        engine.pathing_adaptive_special_distance.argtypes=[u32,u8,u8,u32,u32]
        engine.pathing_adaptive_special_node_state.argtypes=[u32]
    cases=[];publications=[]
    definitions=[
        ('chain',(6.25,6.75),[(1,[312.,568.,712.,968.],(17,19)),(2,[952.,1080.,1352.,1480.],(27,27))]),
        ('equal',(6.25,6.25),[(1,[312.,824.,712.,1224.],(27,27)),(2,[824.,312.,1224.,712.],(27,27))])]
    for topology,source,edges in definitions:
      target=(27.25,27.25)
      for reverse in (False,True):
        for storage,level in zip(data,range(4)):machine.mem_write(storage,bytes((41>>level)**2*8))
        run(0x6f15d360,owner,0,0)
        state=(ctypes.c_uint8*10505)(*state_bytes(initial_classes,[0]*1681))
        for identity,box,destination in (list(reversed(edges)) if reverse else edges):
            machine.mem_write(rectangle,struct.pack('<4f',box[1],box[0],box[3],box[2]));machine.reg_write(UC_X86_REG_EDX,rectangle)
            run(0x6f04e360,identity)
            markers=list(machine.mem_read(data[0],41*41*8))[6::8]
            classbytes=[read(storage+8*i+4)[0]>>24 for level,storage in enumerate(data)for i in range((41>>level)**2)]
            if engine:
                q=(ctypes.c_uint32*8)(64,64,*struct.unpack('<4I',struct.pack('<4f',*box)),0,0)
                engine.pathing_waygate_publish(q,identity,state)
                if list(state)!=state_bytes(classbytes,markers):raise RuntimeError('C multiple-gate publication differs')
                exact_publications+=1
            publications.append(dict(topology=topology,reverse=reverse,identity=identity,box=box,markers=markers,classbytes=classbytes))
        for active,lane,size_input,budget,warp in __import__('itertools').product(range(4),(0,2,4,6),(0,1),(10,400),(0,1)):
            machine.mem_write(source_ptr,struct.pack('<2f',*source));machine.mem_write(target_ptr,struct.pack('<2f',*target))
            records=(ctypes.c_uint32*(256*3))()
            for identity,box,destination in edges:
                enabled=1 if active&(1<<(identity-1)) else 0
                write(special+12*identity,enabled,*destination);records[identity*3:identity*3+3]=[enabled,*destination]
            result=run(0x6f162cb0,system,lane,route,source_ptr,target_ptr,budget,size_input,warp)
            count=read(route+0x1c)[0];nodes_count=read(system+0x6c)[0]
            node_state=[[read(nodes+36*i)[0],read(nodes+36*i+4)[0],read(nodes+36*i+0x14)[0],read(nodes+36*i+0x18)[0],read(nodes+36*i+8)[0],read(nodes+36*i+0x1c)[0],0 if read(nodes+36*i+0xc)[0]==0xffffffff else 1 if read(nodes+36*i+0xc)[0]==0xfffffffe else 2,machine.mem_read(nodes+36*i+0x22,1)[0],machine.mem_read(nodes+36*i+0x20,1)[0],machine.mem_read(nodes+36*i+0x23,1)[0]]for i in range(nodes_count)]
            route_warps=read(system+0xa0)[0];route_words=read(route_data,count*2);route_work=read(system+0x9c)[0]
            distance=run(0x6f1627e0,system,lane,source_ptr,target_ptr,budget,size_input,0x10000420,warp)
            distance_point=read(0x10000420,2);distance_warps=read(system+0xa0)[0]
            if engine:
                classes=[];off=0
                for side in (41,20,10,5):
                    n=side*side
                    classes.extend(state[off+(lane//2)*n:off+(lane//2+1)*n]);off+=4*n
                q=(ctypes.c_uint32*9)(41,41,size_input,budget,*struct.unpack('<4I',struct.pack('<4f',*source,*target)),warp)
                classes=(ctypes.c_uint8*2206)(*classes);markers_c=(ctypes.c_uint8*1681)(*markers)
                out=(ctypes.c_uint32*(5+2*131072))()
                engine.pathing_adaptive_special_route(q,classes,markers_c,records,out)
                wanted=[result,route_work,nodes_count,count,route_warps]+route_words
                if list(out[:len(wanted)])!=wanted:raise RuntimeError('C multiple-gate route/work differs')
                ns=(ctypes.c_uint32*(1+10*nodes_count))();engine.pathing_adaptive_special_node_state(ns)
                if list(ns)!=[nodes_count]+[v for n in node_state for v in n]:raise RuntimeError('C multiple-gate semantic nodes differ')
                ds=(ctypes.c_uint32*6)();engine.pathing_adaptive_special_distance(q,classes,markers_c,records,ds)
                if list(ds)!=[distance,*distance_point,distance_warps,read(system+0x9c)[0],read(system+0x6c)[0]]:raise RuntimeError('C multiple-gate distance accounting differs')
                exact_requests+=1
            cases.append(dict(topology=topology,reverse=reverse,active=active,lane=lane,size_input=size_input,budget=budget,warp=warp,source=list(source),target=list(target),result=result,work=route_work,nodes=node_state,route_words=route_words,route_warps=route_warps,distance=distance,distance_point=distance_point,distance_warps=distance_warps))
        print(topology,reverse,len(cases),flush=True)
    equal=[c for c in cases if c['topology']=='equal' and c['lane']==0 and c['size_input']==0 and c['budget']==400 and c['warp']==1 and not c['reverse']]
    assert [c['active'] for c in equal]==list(range(4))
    assert equal[1]['distance']==equal[2]['distance'], 'alternatives do not have equal native distances'
    payload=dict(binary_sha256=digest,publications=publications,cases=cases,scope='Complete unchanged162cb0/1627e0 requests over supplied empty64x64 fine storage and41/20/10/5 map headers. Two-edge chain and symmetric equal-distance alternatives, both publication orders, four active combinations, four lanes/two sizes/two budgets/warp off-on. No physical-placement or owner stand-ins; public journeys separate.')
    if args.fixture:
        if args.fixture.exists():
            if json.loads(args.fixture.read_text())!=payload:raise RuntimeError('Frozen original multiple-gate result differs')
        else:args.fixture.write_text(json.dumps(payload,separators=(',',':'))+'\n')
    args.report.write_text(json.dumps(dict(passed=True,engine_exact_publications=exact_publications,engine_exact_requests=exact_requests,**payload),indent=2)+'\n')

if __name__=='__main__':main()
