#!/usr/bin/env python3
"""Execute the original mover arrival predicate with synthetic fine-grid inputs.

No function stubs. Covers distance, heading tolerance and forced-range flag;
also checks the target-order range arithmetic slice with supplied locals.
Also executes the post-Path_Advance result slice and forced-arrival clearing.
Does not emulate the full order lifecycle or mover stepping.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import random
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EDI
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path)
    parser.add_argument('--fixture', type=Path, help='write raw original predicate inputs/outputs')
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

    def float_at(address):
        return struct.unpack('<f', machine.mem_read(address, 4))[0]

    floats(0x6fd3c740, -1, 0, 1)
    source, destination, heading, threshold, angle, in_range = [system + o for o in (0x200, 0x210, 0x220, 0x224, 0x228, 0x22c)]
    computed_distance = [None]
    distance_word = [None]

    def observe(machine, address, size, user):
        computed_distance[0] = float_at(machine.reg_read(UC_X86_REG_EBP) + 8)
        distance_word[0] = read(machine.reg_read(UC_X86_REG_EBP) + 8)[0]

    machine.hook_add(UC_HOOK_CODE, observe, begin=0x6f16e97a, end=0x6f16e97a)
    tolerance = float_at(0x6fcd53d4)
    rows = []
    raw_rows = []
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_arrival.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2

    def record():
        inputs = [*read(source, 2), *read(destination, 2), *read(heading), *read(threshold), *read(system + 0xd8)]
        outputs = [distance_word[0], read(angle)[0], read(in_range)[0], machine.reg_read(UC_X86_REG_EAX)]
        if engine:
            actual = (ctypes.c_uint32 * 4)()
            words = (ctypes.c_uint32 * 7)(*inputs)
            engine.pathing_arrival(words, actual)
            assert list(actual) == outputs, (inputs, list(actual), outputs)
            assert list(words) == inputs
        raw_rows.append(dict(input=inputs, output=outputs))
    cases = 0
    for distance, direction, forced in itertools.product(
            [0, 1, 11.25, 11.3125, 11.375, 30],
            [0, 0.1, 0.2, -0.2, 0.20000002, -0.20000002, 1, -1, 3.1415927], [0, 1]):
        floats(source, 0, 0)
        floats(destination, distance, 0)
        floats(heading, direction)
        floats(threshold, 11.3125)
        write(system + 0xd8, forced << 16)
        run(0x6f16e910, system, source, heading, threshold, destination, angle, in_range)
        d = computed_distance[0]
        bits = struct.unpack('<I', struct.pack('<f', d))[0]
        limits = [0, 11.3125, d]
        if bits:
            limits.append(struct.unpack('<f', struct.pack('<I', bits - 1))[0])
        limits.append(struct.unpack('<f', struct.pack('<I', bits + 1))[0])
        for limit in limits:
            floats(threshold, limit)
            run(0x6f16e910, system, source, heading, threshold, destination, angle, in_range)
            actual_angle = float_at(angle)
            expected_range = bool(forced or computed_distance[0] <= float_at(threshold))
            expected_result = expected_range and abs(actual_angle) <= tolerance
            assert read(in_range)[0] == int(expected_range)
            assert machine.reg_read(UC_X86_REG_EAX) == int(expected_result)
            record()
            rows.append(dict(distance=distance, computed_distance=computed_distance[0],
                             heading=direction, threshold=float_at(threshold), forced=forced,
                             result=int(expected_result), in_range=int(expected_range), angle=actual_angle))
            cases += 1
    # Oblique and translated vectors, minimum-range/equality neighbours and
    # signed angular/deadzone neighbours execute the complete original call.
    for origin, vector, direction, flags in itertools.product(
            [(0, 0), (163.5, 91.5), (-100.125, -21.875)],
            [(0, 0), (.3125, 0), (.3, .4), (-.3, .4), (-.3, -.4), (.3, -.4), (1e-7, 0), (3, 4)],
            [0, 2e-7, -2e-7, .2, .20000002, -.2, -.20000002, 1.5707963, 3.1415927, 6.2831855],
            [0, 0x10000]):
        floats(source, *origin)
        floats(destination, *(o + v for o, v in zip(origin, vector)))
        floats(heading, direction)
        floats(threshold, .49)
        write(system + 0xd8, flags)
        run(0x6f16e910, system, source, heading, threshold, destination, angle, in_range)
        distance_bits = distance_word[0]
        for limit in set([0x3efae148, distance_bits, max(0, distance_bits - 1), distance_bits + 1]):
            write(threshold, limit)
            run(0x6f16e910, system, source, heading, threshold, destination, angle, in_range)
            record()
    # Original target-order arithmetic slice. Earlier wrapper code converts
    # mover footprints to world units; provide those two locals explicitly.
    range_cases = 0
    frame = stack - 0x1000
    minimum_range = float_at(0x6fcd53f0)
    range_input = system + 0x240
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    for authored, first, second in itertools.product([0, 1, 32, 100, 300, 500], [0, 1, 16, 31, 64], [0, 1, 16, 31, 64]):
        floats(range_input, authored)
        write(frame + 0xc, range_input)
        floats(frame - 0x18, first, second)
        machine.reg_write(UC_X86_REG_EBP, frame)
        machine.reg_write(UC_X86_REG_ESP, frame - 0x100)
        machine.emu_start(0x6f05a702, 0x6f05a764, count=10000)
        assert machine.reg_read(UC_X86_REG_EIP) == 0x6f05a764
        expected = max(minimum_range, f32(f32(authored + first) + second) / 32)
        assert float_at(frame + 8) == expected
        range_cases += 1

    force_cases = 0
    for result, ready, flags in itertools.product([0, 1, 2, 3, 4, 5, 0x100000, 0xffffffff],
                                                 [0, 1, 2], [0, 1, 0x10000, 0xffffffff]):
        write(system + 0xd8, flags)
        write(output, 0)
        write(frame - 4, ready)
        write(frame + 0x20, output, 1)
        machine.reg_write(UC_X86_REG_EBP, frame)
        machine.reg_write(UC_X86_REG_ESP, frame - 0x100)
        machine.reg_write(UC_X86_REG_EDI, system)
        machine.reg_write(UC_X86_REG_EAX, result)
        machine.emu_start(0x6f16fd0a, 0x6f16fd3b, count=100)
        assert machine.reg_read(UC_X86_REG_EIP) == 0x6f16fd3b
        force = ready != 0 and result in (3, 4)
        assert read(system + 0xd8)[0] == (flags | (0x10000 if force else 0))
        assert read(output)[0] == int(ready != 0 and result == 4)
        assert read(frame + 0x24)[0] == int(result != 0)
        run(0x6f16f560, system)
        assert read(system + 0xd8)[0] == flags & ~0x10000
        force_cases += 1

    report = dict(binary_sha256=digest, scope=__doc__, passed=True, cases=cases,
                  angle_tolerance=tolerance, range_slice_cases=range_cases, minimum_fine_range=minimum_range, force_result_slice_cases=force_cases,
                  engine_cases=len(raw_rows) if engine else 0,
                  limitation='Complete predicate port uses supplied fine-grid geometry/flags; public producer lifecycle and engine cadence are separate.',
                  observations=rows)
    if args.fixture:
        fixture = dict(binary_sha256=digest, scope=report['limitation'], cases=raw_rows)
        args.fixture.write_text(json.dumps(fixture, separators=(',', ':')) + '\n')
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'observations'}, indent=2))


if __name__ == '__main__':
    main()
