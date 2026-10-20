#!/usr/bin/env python3
"""Verify destination-cell change and timestamp gating in original retail x86.

No stubs; synthetic coordinates, owner counter and path timestamps. Does not
execute target tracking, path reset, admission or movement.
"""
import argparse
import hashlib
import math
import itertools
import json
import random
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
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

    machine.mem_map(0x10000000, 0x20000)
    machine.mem_map(0x20000000, 0x10000)
    system, output = 0x10000000, 0x10000800
    stack, stop = 0x20008000, 0x30000000

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, machine.mem_read(address, count * 4)))

    def run(entry, self, *arguments):
        write(stack, stop, *arguments)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, self)
        machine.emu_start(entry, stop, count=10000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail scheduler operation exceeded instruction budget')

    def floats(address, *values):
        machine.mem_write(address, struct.pack('<' + 'f' * len(values), *values))

    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    floats(0x6fd3c740, -1, 0, 1)
    owner, destination = system + 0x1000, system + 0x2000
    write(0x6fd53a48, owner)
    values = [-4, -2.01, -2, -1.99, -1, -0.01, -0.0, 0, 0.01, 0.99, 1, 1.99, 2, 2.01, 3.99, 4, 100, 101, 102]
    times = [(100, 90, 90), (100, 91, 90), (100, 90, 91), (100, 91, 91),
             (0, 0, 0), (9, 0, 0), (5, 0xfffffffa, 0xfffffffa), (5, 10, 10)]
    cases = 0
    outcomes = {'unchanged': 0, 'changed_ready': 0, 'changed_delayed': 0}
    for old, new, axis, shift, timing in itertools.product(values, values, range(2), [0, 1, 2, 31, 33], times):
        source_xy, dest_xy = [0.25, 0.25], [0.25, 0.25]
        source_xy[axis], dest_xy[axis] = old, new
        floats(system + 0x1c, *source_xy)
        floats(destination, *dest_xy)
        current, fine_stamp, acc_stamp = timing
        write(system + 0x7c, fine_stamp, acc_stamp)
        write(owner + 0x538, current)
        before = bytes(machine.mem_read(system, 0xb8))
        write(output, 0xdeadbeef)
        run(0x6f167e40, system, destination, shift, output)
        changed = (math.floor(f32(old)) >> (shift & 31)) != (math.floor(f32(new)) >> (shift & 31))
        ready = not changed or (((current - fine_stamp) & 0xffffffff) >= 10 and
                                ((current - acc_stamp) & 0xffffffff) >= 10)
        assert machine.reg_read(UC_X86_REG_EAX) == int(changed)
        assert read(output)[0] == int(ready)
        assert bytes(machine.mem_read(system, 0xb8)) == before
        assert read(owner + 0x538)[0] == current
        outcomes['unchanged' if not changed else 'changed_ready' if ready else 'changed_delayed'] += 1
        cases += 1
    report = dict(binary_sha256=digest, scope=__doc__, passed=True, cases=cases,
                  outcomes=outcomes, coordinate_values=values, shifts=[0, 1, 2, 31, 33],
                  timing_cases=times, runtime_constants={'minus_one': -1, 'zero': 0, 'one': 1})
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
