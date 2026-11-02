#!/usr/bin/env python3
"""Verify target refresh countdown and distance-dependent reload in retail x86.

Full non-reload routine; reload slice starts after member position conversion,
with synthetic fine-grid position locals. No function stubs; original distance
and deterministic float helpers execute. Clamp is checked independently from
the observed original integer candidate, not IEEE float approximations.
Also covers request flag setter and request-to-group copy; complete group
completion routine with zero members; sampler tail with explicit callback result
and nonzero countdown; temporary arrival-range override/restore slices. These
slices do not execute target resolution, visibility, or member movement.
"""
import argparse
import hashlib
import itertools
import json
import random
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EDX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--coefficient', type=float, default=0.33)
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

    floats(0x6fd3c740, -1, 0, 1)
    floats(0x6fd541b8, args.coefficient)
    frame, path = stack - 0x1000, system + 0x1000
    observed = {}
    def observe(machine, address, size, data):
        if address == 0x6f169727:
            observed['distance'] = struct.unpack('<f', machine.mem_read(frame - 8, 4))[0]
        else:
            value = machine.reg_read(UC_X86_REG_EAX)
            observed['candidate'] = value if value < 0x80000000 else value - 0x100000000
    machine.hook_add(UC_HOOK_CODE, observe, begin=0x6f169727, end=0x6f169727)
    machine.hook_add(UC_HOOK_CODE, observe, begin=0x6f16974d, end=0x6f16974d)
    rows = []
    for distance, extra in itertools.product(range(0, 2001), range(2)):
        write(system + 0x3c, path)
        write(system + 0x64, 16)
        write(system + 0x80, extra * 0x400)
        floats(path + 0x1c, distance / 4, 0)
        floats(frame - 0x2c, 0, 0)
        machine.reg_write(UC_X86_REG_EBX, system)
        machine.reg_write(UC_X86_REG_EBP, frame)
        machine.reg_write(UC_X86_REG_ESP, frame - 0x100)
        machine.emu_start(0x6f1696c8, 0x6f169781, count=10000)
        assert machine.reg_read(UC_X86_REG_EIP) == 0x6f169781
        expected = min(132, max(16, observed['candidate'])) + 165 * extra
        assert read(system + 0x64)[0] == expected
        rows.append(dict(input_distance=distance / 4, computed_distance=observed['distance'],
                         candidate=observed['candidate'], extra=bool(extra), reload=expected))
    countdown_cases = 0
    for initial in range(298):
        write(system + 0x64, initial)
        run(0x6f169680, system)
        assert read(system + 0x64)[0] == max(0, initial - 1)
        countdown_cases += 1
    flag_cases = 0
    request = system + 0x3000
    for low_flags, enabled in itertools.product(range(4096), [0, 1, 2]):
        flags = 0xa0000000 | low_flags
        write(request + 0x100, flags)
        run(0x6f16dc00, request, enabled)
        expected = flags | 0x400 if enabled else flags & ~0x400
        assert read(request + 0x100)[0] == expected
        machine.reg_write(UC_X86_REG_EDI, request)
        machine.reg_write(UC_X86_REG_ESI, system)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.emu_start(0x6f16be35, 0x6f16be43, count=10)
        assert machine.reg_read(UC_X86_REG_EIP) == 0x6f16be43
        assert read(system + 0x80)[0] == expected
        flag_cases += 1

    completion_cases = 0
    for flags, missed, progress in itertools.product(
            [0, 1, 0x800, 0x801, 0xffffffff],
            list(range(66)) + [0x7fffffff, 0x80000000, 0xffffffff],
            [0, 19, 0xffffffff]):
        write(system + 0x38, 0)  # Empty member list: full routine, gate only.
        write(system + 0x60, progress)
        write(system + 0x6c, missed)
        write(system + 0x80, flags)
        run(0x6f16c390, system)
        entered = not flags & 1 or missed >= 33
        assert read(system + 0x60)[0] == (progress + int(entered)) & 0xffffffff
        assert read(system + 0x6c)[0] == missed
        completion_cases += 1

    hidden_sample_cases = 0
    for blocked, missed, countdown in itertools.product(
            [0, 1, 2], [0, 1, 31, 32, 33, 0xffffffff], [1, 16, 132, 297]):
        write(system + 0x3c, path)
        write(system + 0x64, countdown)
        write(system + 0x6c, missed)
        floats(path + 0x1c, 163.5, 91.5)
        write(frame, 0, stop, output)
        machine.reg_write(UC_X86_REG_EBP, frame)
        machine.reg_write(UC_X86_REG_ESP, frame - 0x1c)
        machine.reg_write(UC_X86_REG_ESI, system)
        machine.reg_write(UC_X86_REG_EAX, blocked)
        machine.emu_start(0x6f16cd6e, stop, count=100)
        assert machine.reg_read(UC_X86_REG_EIP) == stop
        assert read(system + 0x6c)[0] == ((missed + 1) & 0xffffffff if blocked else 0)
        assert read(system + 0x64)[0] == countdown
        assert read(output, 2) == read(path + 0x1c, 2)
        hidden_sample_cases += 1

    range_override_cases = 0
    mover = system + 0x5000
    for missed, radius in itertools.product([0, 1, 32, 33, 0xffffffff], [0.0, 0.49, 11.3125, 100.0]):
        write(system + 0x6c, missed)
        floats(mover + 0xb0, radius)
        original = read(mover + 0xb0)[0]
        machine.reg_write(UC_X86_REG_EDX, system)
        machine.reg_write(UC_X86_REG_ECX, mover)
        machine.emu_start(0x6f16a88b, 0x6f16a8a2, count=20)
        assert read(mover + 0xb0)[0] == (read(0x6fcd53f0)[0] if missed else original)
        machine.reg_write(UC_X86_REG_EAX, mover)
        machine.emu_start(0x6f16a977, 0x6f16a983, count=20)
        assert read(mover + 0xb0)[0] == original
        range_override_cases += 1

    report = dict(binary_sha256=digest, scope=__doc__, passed=True,
                  coefficient=struct.unpack('<f', machine.mem_read(0x6fd541b8, 4))[0],
                  reload_cases=len(rows), countdown_cases=countdown_cases,
                  request_flag_and_copy_cases=flag_cases,
                  completion_gate_cases=completion_cases, hidden_sample_cases=hidden_sample_cases,
                  range_override_cases=range_override_cases, observations=rows)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'observations'}, indent=2))


if __name__ == '__main__':
    main()
