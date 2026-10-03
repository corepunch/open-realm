#!/usr/bin/env python3
"""Execute the complete original speed-cap transition, including old-velocity integration.

Constructed existing mover, clocks and both real spatial maps; no function stubs.
Public creation, owner clock production and region/presentation callbacks are excluded.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import struct
from pathlib import Path

GAME_SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EIP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path)
    parser.add_argument('--fixture', type=Path)
    args = parser.parse_args()
    binary = args.binary.read_bytes()
    assert hashlib.sha256(binary).hexdigest() == GAME_SHA
    pe = struct.unpack_from('<I', binary, 0x3c)[0]; opt = pe + 24
    base, size = [struct.unpack_from('<I', binary, opt + o)[0] for o in (28, 56)]
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (size + 4095) & ~4095)
    machine.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + i * 40
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count: machine.mem_write(base + va, binary[offset:offset + count])
    machine.mem_map(0x10000000, 0x20000); machine.mem_map(0x20000000, 0x10000)
    system, mover, owner, cap_ptr, stack, stop = 0x10000000, 0x10001000, 0x10002000, 0x10003000, 0x20008000, 0x30000000
    objects = [system + 0x4000, system + 0x4100]
    maps = [system + 0x5000, system + 0x5200]
    cells = [system + 0x6000, system + 0x7000]
    links = [system + 0x8000, system + 0xa000]
    bitmaps = [system + 0xc000, system + 0xc100]
    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))
    def read(address, n):
        return list(struct.unpack('<' + 'I' * n, machine.mem_read(address, n * 4)))
    def bits(value):
        return struct.unpack('<I', struct.pack('<f', value))[0]
    def floats(address, *values):
        write(address, *[bits(v) for v in values])
    write(0x6fd53a48, owner)
    floats(0x6fd3c740, -1, 0, 1, 2)
    changed = [False]
    def branch(uc, address, size, data):
        changed[0] = True
    machine.hook_add(UC_HOOK_CODE, branch, begin=0x6f15ffa1, end=0x6f15ffa1)
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine: engine.pathing_speed_cap.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
    cases = []
    velocities = [(0, 0), (4.6875, 0), (0, 12.5), (3, 4), (-3, 4), (3, -4), (-3, -4), (.00025, -.00025)]
    velocities += [(struct.unpack('<f', struct.pack('<I', 0x31c80000))[0], struct.unpack('<f', struct.pack('<I', 0x4147ffff))[0])]
    for velocity, cap, elapsed, domain, epoch in itertools.product(velocities, [0, .000125, 1, 4.6875, 5, 12.5, 16], [0, .03125, .125], [0, 0x80000000], [0, 1]):
        machine.mem_write(mover, bytes(0x100)); machine.mem_write(owner, bytes(0x100))
        clock = owner + (0x68 if domain else 0x14)
        write(mover + 0x14, domain)
        floats(clock + 0x40, elapsed); write(clock + 0x44, epoch); floats(clock + 0x48, .25)
        floats(mover + 0x70, 0); write(mover + 0x74, 0)
        floats(mover + 0x78, 8, 8, *velocity, 16, .125)
        floats(mover + 0x90, .25); write(mover + 0x94, *objects)
        for obj, grid, data, records, bitmap in zip(objects, maps, cells, links, bitmaps):
            machine.mem_write(obj, bytes(0x80)); machine.mem_write(grid, bytes(0x100))
            write(obj + 0x2c, grid); write(obj + 0x34, 0x01000001); write(obj + 0x40, 0x20000123)
            write(grid + 0x28, data); write(grid + 0x3c, 16, 16); write(grid + 0x54, 0, 0, 16, 16)
            floats(grid + 0x68, 1); write(grid + 0x78, records); write(grid + 0x84, 1024, 0)
            write(grid + 0x98, bitmap); write(grid + 0xac, 0xffffff)
            machine.mem_write(bitmap, bytes(32)); machine.mem_write(data, struct.pack('<256I', *([0xffffff] * 256)))
        floats(cap_ptr, cap)
        inputs = read(mover + 0x70, 8) + read(clock + 0x40, 3) + [bits(cap), read(objects[1] + 0x40, 1)[0]]
        write(stack, stop, cap_ptr); machine.reg_write(UC_X86_REG_ESP, stack); machine.reg_write(UC_X86_REG_ECX, mover)
        changed[0] = False
        machine.emu_start(0x6f15ff40, stop, count=100000)
        assert machine.reg_read(UC_X86_REG_EIP) == stop
        output = read(mover + 0x70, 8) + [read(objects[1] + 0x40, 1)[0], int(changed[0])]
        if engine:
            actual = (ctypes.c_uint32 * 10)()
            engine.pathing_speed_cap((ctypes.c_uint32 * 13)(*inputs), actual)
            assert list(actual) == output, (inputs, list(actual), output)
        cases.append(dict(input=inputs, output=output))
    result = dict(binary_sha256=GAME_SHA, passed=True, cases=len(cases), engine_cases=len(cases) if engine else 0,
                  integrated_cases=sum(c['output'][-1] for c in cases), scope=__doc__)
    if args.fixture: args.fixture.write_text(json.dumps(dict(binary_sha256=GAME_SHA, cases=cases, scope=__doc__), separators=(',', ':')) + '\n')
    args.report.write_text(json.dumps(result, indent=2) + '\n'); print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
