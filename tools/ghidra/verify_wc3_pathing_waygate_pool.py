#!/usr/bin/env python3
"""Original Way Gate 1..255 allocation, zero exhaustion, active-count/getter and release.

Complete original CPaWarp constructor and map bridges execute unchanged.
Owner/availability storage is supplied; external Storm imports receive host
backing. This does not certify source rectangle overlap or portal trajectories.
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
    from unicorn.x86_const import UC_X86_REG_EDX
    engine=ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    used=(ctypes.c_uint8*256)()
    exact=0
    if engine:engine.pathing_waygate_id_allocate.argtypes=[ctypes.POINTER(ctypes.c_uint8)]
    mapowner,owner,ids=0x10804000,0x10802000,0x10900000
    write(0x6fd3c82c,mapowner);write(mapowner+0x4c,256,ids,0)
    write(0x6fd53a48,owner);write(owner+0x250,fine)
    initial=read(fine+0x3c)[0]
    records=[]
    for i in range(256):
        value=run(0x6f04e510,0);records.append(value)
        if value!=(i+1 if i<255 else 0):raise RuntimeError('bad ID sequence')
        if engine:
            if engine.pathing_waygate_id_allocate(used)!=value or list(used)!=list(uc.mem_read(ids,256)):raise RuntimeError('C allocation/bitmap differs')
            exact+=1
    def set_active(identity,active):
        uc.reg_write(UC_X86_REG_EDX,active);run(0x6f04e210,identity)
        return [read(mapowner+0x54)[0],read(initial+12*identity)[0]&1,run(0x6f04e290,identity)]
    active=[]
    for identity in records:active.append(dict(identity=identity,first=set_active(identity,1),repeat=set_active(identity,1)))
    full=list(uc.mem_read(ids,256))
    for identity in [0,17,255,17,256]:run(0x6f04e4c0,identity)
    freed=list(uc.mem_read(ids,256));count=read(mapowner+0x54)[0]
    refill=[]
    for i in range(3):
        value=run(0x6f04e510,0);refill.append(value)
        if engine:
            for identity in [17,255]:used[identity]=freed[identity]
            if i: # Retain earlier allocations in this refill.
                for prior in refill[:-1]:used[prior]=1
            if engine.pathing_waygate_id_allocate(used)!=value or list(used)!=list(uc.mem_read(ids,256)):raise RuntimeError("C refill bitmap differs")
            exact+=1
    print('IDs',records[-4:],'active tail',active[-3:],'count',count,'refill',refill,flush=True)
    payload=dict(binary_sha256=HASH,ids=records,active=active,full=full,freed=freed,active_count=count,refill=refill)
    if args.fixture:
        if args.fixture.exists():
            if json.loads(args.fixture.read_text())!=payload:raise RuntimeError('frozen Way Gate pool differs')
        else:args.fixture.write_text(json.dumps(payload,indent=2)+'\n')
    args.report.write_text(json.dumps(dict(passed=True,engine_exact_allocations=exact,**payload),indent=2)+'\n')

if __name__=='__main__':main()
