#!/usr/bin/env python3
"""Original fine storage growth, 32768-node refusal and subsequent requests.

The game DLL's containers, memory-block wrapper, metadata links and search run
unchanged. Only external Storm allocation imports receive host storage. This
does not emulate Storm OOM, the public movement scheduler or full map loading.
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
        class Objects(ctypes.Structure):
            _fields_ = [('cells', ctypes.POINTER(ctypes.c_uint8)), ('objects', ctypes.POINTER(ctypes.c_uint32))]
        engine.pathing_fine_request_words.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(Objects), ctypes.POINTER(ctypes.c_uint32)]
        engine.pathing_fine_storage.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
        engine.pathing_fine_node_state.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
        engine.pathing_fine_storage(1, (ctypes.c_uint32*3)())
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
    uc.mem_map(0x10000000, 0x200000)
    uc.mem_map(0x20000000, 0x10000)
    uc.mem_map(0x40000000, 0x4000000)
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
            if next_block > 0x44000000:
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

    for address, value in ((0x6fd3c740, -1.0), (0x6fd3c744, 0.0), (0x6fd3c748, 1.0)):
        uc.mem_write(address, struct.pack('<f', value))
    run(0x6f147600, fine)
    write(fine + 0x1c, tilemap)
    write(tilemap + 0x28, cells)
    write(tilemap + 0x38, 65536, 256, 256)
    write(tilemap + 0x98, bitmap)
    write(tilemap + 0xac, 0xffffff)
    write(tilemap + 0xa8, 2048)
    # Initialized empty cell-link collection; its original append/growth runs.
    write(tilemap + 0x6c, 0, -1, 0, -1, 0, 1024, 0, 0)
    write(route + 0xc, route_data)
    write(route + 0x18, 32768, 0)
    write(mask, 0x02000002)
    write(radius, 0)
    records = []

    def request(name, start, goal, budget):
        write(source, *(struct.unpack('<I', struct.pack('<f', v))[0] for v in start))
        write(target, *(struct.unpack('<I', struct.pack('<f', v))[0] for v in goal))
        result = run(0x6f148100, fine, route, source, target, mask, budget, radius, 0)
        nodes, count = read(fine+0x30)[0], read(fine+0x40)[0]
        route_count = read(route+0x1c)[0]
        state = []
        for i in range(count):
            n = read(nodes+36*i, 9)
            state.append([n[0], n[1], n[5], n[6], n[2], n[7], 0 if n[3] == 0xffffffff else 1 if n[3] == 0xfffffffe else 2])
        node_fnv64 = 14695981039346656037
        for n in state:
            for byte in struct.pack('<7I',*n):
                node_fnv64 = ((node_fnv64 ^ byte) * 1099511628211) & 0xffffffffffffffff
        row = dict(name=name, start=start, goal=goal, budget=budget, result=result,
                   nodes=count, work=read(fine+0x6c)[0], nearest=read(nodes+36*read(fine+0x9c)[0], 2),
                   distance=read(fine+0x98)[0], route_words=read(route_data, 2*route_count),
                   node_sha256=hashlib.sha256(json.dumps(state,separators=(',',':')).encode()).hexdigest(),
                   node_fnv64=f'{node_fnv64:016x}',
                   node_capacity=read(fine+0x3c)[0], heap_capacity=read(fine+0x5c)[0],
                   link_count=read(tilemap+0x88)[0], metadata_count=read(tilemap+0xb0)[0],
                   free_head=read(tilemap+0xac)[0])
        records.append(row)
        if engine:
            words = [struct.unpack('<I', struct.pack('<f', v))[0] for v in (*start, *goal)]
            query = (ctypes.c_uint32*15)(256,256,int(start[0]),int(start[1]),int(goal[0]),int(goal[1]),budget,0,0x02000002,0,0,*words)
            terrain_c = (ctypes.c_uint8*65536)(*(v>>24 for v in terrain))
            output = (ctypes.c_uint32*65542)()
            engine.pathing_fine_request_words(query, ctypes.byref(Objects(terrain_c,None)), output)
            if list(output[:4]) != [result,row['work'],count,route_count] or list(output[6:6+2*route_count]) != row['route_words']:
                raise RuntimeError(f'production C request differs: {name}, {list(output[:4])}')
            states = (ctypes.c_uint32*(1+7*32768))()
            engine.pathing_fine_node_state(states)
            if [list(states[1+7*i:8+7*i]) for i in range(states[0])] != state:
                raise RuntimeError(f'production C node state differs: {name}')
            capacities = (ctypes.c_uint32*3)()
            engine.pathing_fine_storage(0,capacities)
            if list(capacities[:2]) != [row['node_capacity'],row['heap_capacity']]:
                raise RuntimeError(f'production C capacity differs: {name}, {list(capacities)}')
        print(name, 'result', result, 'nodes', count, 'work', row['work'], flush=True)

    terrain = [0x02ffffff if x >= 192 or (abs(x-128) <= 8 and abs(y-128) <= 8) else 0x00ffffff
               for y in range(256) for x in range(256)]
    uc.mem_write(cells, struct.pack('<65536I', *terrain))
    request('engine_budget_growth', [118.125,118.875], [128.25,128.75],2048)
    request('node_cap_disconnected', [4.125, 4.875], [220.25, 200.75], 100000)
    if records[-1]['nodes'] != 32768 or records[-1]['result'] != 0:
        raise RuntimeError('fixture did not naturally exhaust the native node cap')
    vacant = next((x,y) for y in range(256) for x in range(192) if read(cells+4*(y*256+x))[0]&0xffffff == 0xffffff)
    refusal = dict(existing=run(0x6f147af0,fine,4,4), new=run(0x6f147af0,fine,*vacant),
                   outside=run(0x6f147af0,fine,256,0), vacant=list(vacant))
    if refusal['existing'] != 0 or refusal['new'] != 0xffffffff or refusal['outside'] != 0xffffffff or read(fine+0x40)[0] != 32768:
        raise RuntimeError('native capacity refusal/cache contract differs')
    request('after_capacity_short', [4.125, 4.875], [20.25, 19.75], 700)
    request('after_capacity_reverse', [20.25, 19.75], [4.125, 4.875], 700)
    metadata_slots = read(tilemap+0xb0)[0]
    run(0x6f14dfc0,tilemap)
    free_slots = []
    at = read(tilemap+0xac)[0]
    while at != 0xffffff:
        free_slots.append(at)
        if len(free_slots) > metadata_slots: raise RuntimeError('native metadata free-list cycle')
        at = read(read(tilemap+0x78)[0]+8*at)[0]&0xffffff
    if len(set(free_slots)) != metadata_slots or read(tilemap+0xb0)[0] != 0:
        raise RuntimeError('native compaction did not recycle all search links')
    compaction = dict(free_slots=len(free_slots),head=free_slots[0],
                      sha256=hashlib.sha256(json.dumps(free_slots,separators=(',',':')).encode()).hexdigest())
    before_allocations = len(growth)
    request('after_compaction_short', [4.125,4.875], [20.25,19.75],700)
    if len(growth) != before_allocations or records[-1]['link_count'] != metadata_slots or records[-1]['metadata_count'] != 108:
        raise RuntimeError('next request did not reuse compacted free slots')
    payload = dict(binary_sha256=HASH, scope=__doc__, cases=records, allocation_growth=growth,
                   capacity_refusal=refusal,compaction=compaction)
    if args.fixture:
        if args.fixture.exists():
            if json.loads(args.fixture.read_text()) != payload:
                raise RuntimeError('frozen storage fixture differs')
        else:
            args.fixture.write_text(json.dumps(payload,indent=2)+'\n')
    args.report.write_text(json.dumps(dict(passed=True,engine_exact_requests=len(records) if engine else 0,**payload),indent=2)+'\n')


if __name__ == '__main__':
    main()
