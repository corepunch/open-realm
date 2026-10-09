#!/usr/bin/env python3
"""Original source publication/overlap erasure and64 complete ordinary requests.

World rectangle, fine clipping, base marker writes, three parent reducers and
complete adaptive requests execute unchanged over supplied empty fine storage,
map/owner headers and retained work buffers. Public stock-gate producer repeats
are a separate fixture. No portal search or physical traversal claim.
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
    source, target = (4.25, 4.75), (27.25, 27.75)
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
        engine.pathing_adaptive_route.argtypes=[u32,u8,u32]
        engine.pathing_adaptive_node_state.argtypes=[u32]
    for ordering,boxes in [('AB',[A,B]),('BA',[B,A])]:
        for storage,level in zip(data,range(4)):machine.mem_write(storage,bytes((41>>level)**2*8))
        run(0x6f15d360,owner,0,0)
        state=(ctypes.c_uint8*10505)(*state_bytes(initial_classes,[0]*1681))
        for identity,box in [(1,boxes[0]),(2,boxes[1]),(0,boxes[0]),(0,boxes[1])]:
            machine.mem_write(rectangle,struct.pack('<4f',box[1],box[0],box[3],box[2]));machine.reg_write(UC_X86_REG_EDX,rectangle)
            run(0x6f04e360,identity)
            markers=list(machine.mem_read(data[0],41*41*8))[6::8]
            classbytes=[read(storage+8*i+4)[0]>>24 for level,storage in enumerate(data)for i in range((41>>level)**2)]
            if engine:
                q=(ctypes.c_uint32*8)(64,64,*struct.unpack('<4I',struct.pack('<4f',*box)),0,0)
                engine.pathing_waygate_publish(q,identity,state)
                if list(state)!=state_bytes(classbytes,markers):raise RuntimeError('C marker/parent publication differs')
                exact_publications+=1
            searches=[]
            for lane in (0,2,4,6):
                for size_input in (0,1):
                    machine.mem_write(source_ptr,struct.pack('<2f',4.25,4.75));machine.mem_write(target_ptr,struct.pack('<2f',27.25,27.75))
                    result=run(0x6f162cb0,system,lane,route,source_ptr,target_ptr,400,size_input,0)
                    count=read(route+0x1c)[0];nodes_count=read(system+0x6c)[0]
                    node_state=[[read(nodes+36*i)[0],read(nodes+36*i+4)[0],read(nodes+36*i+0x14)[0],read(nodes+36*i+0x18)[0],read(nodes+36*i+8)[0],read(nodes+36*i+0x1c)[0],0 if read(nodes+36*i+0xc)[0]==0xffffffff else 1 if read(nodes+36*i+0xc)[0]==0xfffffffe else 2,machine.mem_read(nodes+36*i+0x22,1)[0]]for i in range(nodes_count)]
                    if engine:
                        classes=[];off=0
                        for side in (41,20,10,5):
                            n=side*side;classes.extend(state[off+(lane//2)*n:off+(lane//2+1)*n]);off+=4*n
                        out=(ctypes.c_uint32*(4+2*65536))()
                        q=(ctypes.c_uint32*8)(41,41,size_input,400,*struct.unpack('<4I',struct.pack('<4f',4.25,4.75,27.25,27.75)))
                        engine.pathing_adaptive_route(q,(ctypes.c_uint8*2206)(*classes),out)
                        expected=[result,read(system+0x9c)[0],nodes_count,count]+read(route_data,count*2)
                        if list(out[:len(expected)])!=expected:raise RuntimeError('C complete ordinary route/work differs')
                        ns=(ctypes.c_uint32*(1+8*nodes_count))();engine.pathing_adaptive_node_state(ns)
                        if list(ns)!=[nodes_count]+[v for n in node_state for v in n]:raise RuntimeError('C final semantic nodes differ')
                        exact_requests+=1
                    searches.append(dict(lane=lane,size_input=size_input,result=result,work=read(system+0x9c)[0],nodes=nodes_count,route_words=read(route_data,count*2),node_state=node_state))
            observations.append(dict(ordering=ordering,identity=identity,box=box,markers=markers,classbytes=classbytes,searches=searches))
            print(ordering,identity,'markers',dict(collections.Counter(markers)),'searches',[(r['size_input'],r['work'],r['nodes'])for r in searches[:2]],flush=True)
    boundaries=[]
    for origin in ((0.,0.),(-2048.25,-1024.5)):
        write(game+0x6c,*struct.unpack('<2I',struct.pack('<2f',*origin)))
        for relative in ((548.,312.,312.,79.),(-63.5,-64.,2177.,2080.),(10.,10.,10.,10.),(0.,0.,31.5,95.5)):
            box=[relative[k]+origin[k%2]for k in range(4)]
            for identity in (1,255,0):
                for storage,level in zip(data,range(4)):machine.mem_write(storage,bytes((41>>level)**2*8))
                run(0x6f15d360,owner,0,0)
                state=(ctypes.c_uint8*10505)(*state_bytes(initial_classes,[0]*1681))
                seed=[origin[0],origin[1],origin[0]+2048,origin[1]+2048]if identity==0 else None
                for marker,rect in ([(1,seed)]if seed else [])+[(identity,box)]:
                    machine.mem_write(rectangle,struct.pack('<4f',rect[1],rect[0],rect[3],rect[2]));machine.reg_write(UC_X86_REG_EDX,rectangle)
                    run(0x6f04e360,marker)
                    if engine:
                        q=(ctypes.c_uint32*8)(64,64,*struct.unpack('<4I',struct.pack('<4f',*rect)),*struct.unpack('<2I',struct.pack('<2f',*origin)))
                        engine.pathing_waygate_publish(q,marker,state)
                markers=list(machine.mem_read(data[0],41*41*8))[6::8]
                classbytes=[read(storage+8*i+4)[0]>>24 for level,storage in enumerate(data)for i in range((41>>level)**2)]
                if engine and list(state)!=state_bytes(classbytes,markers):raise RuntimeError('C boundary marker/parent differs')
                boundaries.append(dict(origin=list(origin),box=box,identity=identity,seed=seed,markers=markers,classbytes=classbytes))
    payload=dict(boundaries=boundaries,binary_sha256=digest,initial_classes=initial_classes,observations=observations,
        scope='Original04e360 world rectangle,15c000 integer clip/base marker writer and three parent reducers; original15d360 empty fine terrain producer over supplied64x64 storage and41/20/10/5 padded headers;64 complete ordinary warp-disabled requests. No stubs, overlapping creations in both orders and unconditional rectangle cleanup.')
    if args.fixture:
        if args.fixture.exists():
            if json.loads(args.fixture.read_text())!=payload:raise RuntimeError('Frozen original overlap differs')
        else:args.fixture.write_text(json.dumps(payload,indent=2)+'\n')
    args.report.write_text(json.dumps(dict(passed=True,engine_exact_publications=exact_publications,engine_exact_requests=exact_requests,engine_exact_boundaries=len(boundaries)if engine else 0,**payload),indent=2)+'\n')

if __name__=='__main__':main()
