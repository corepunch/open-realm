#!/usr/bin/env python3
"""Execute the original straight-segment sampler with real footprint/cell checks.

Cardinal unit directions avoid assuming unrecovered normalization parity.
Checks visited-cell order, early rejection and endpoint/short-segment behavior.
Also runs complete waypoint selection and index/point commit for class zero.
The composed model uses measured original normalization outputs, not a
bit-parity replacement normalizer. No stubs or retail bytes included.
"""
import argparse
import hashlib
import itertools
import json
import math
import random
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EBP
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
        machine.emu_start(entry, stop, count=100000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail target lookup exceeded instruction budget')

    fine, grid, cells = system + 0x1000, system + 0x2000, system + 0x3000
    start, direction = system + 0x4000, system + 0x4100
    write(fine + 0x1c, grid)
    write(fine + 0xa4, 0x02000000)
    write(grid + 0x28, cells)
    write(grid + 0x3c, 16, 16)
    write(0x6fd53a84, fine)
    write(0x6fd53a80, 0)
    machine.mem_write(0x6fd3c740, struct.pack('<3f', -1, 0, 1))
    clear = struct.pack('<I', 0xffffff) * 256
    visited = []

    def visit(uc, address, size, data):
        sp = uc.reg_read(UC_X86_REG_ESP)
        visited.append(tuple(struct.unpack('<2i', uc.mem_read(sp + 4, 8))))

    machine.hook_add(UC_HOOK_CODE, visit, begin=0x6f1489a0, end=0x6f1489a0)
    cases = 0
    short_cases = 0
    for position, delta, length, blocked in itertools.product(
            [(8.25, 8.75), (0.25, 0.75), (1.25, 1.75)],
            [(1,0), (-1,0), (0,1), (0,-1)],
            [0, 0.5, 1, 1.125, 2, 2.5, 5],
            [None] + [(x,y) for y in range(16) for x in range(16)]):
        machine.mem_write(cells, clear)
        if blocked is not None:
            write(cells + (blocked[1]*16 + blocked[0])*4, 0x02ffffff)
        machine.mem_write(start, struct.pack('<2f', *position))
        machine.mem_write(direction, struct.pack('<2f', *delta))
        expected, previous, result = [], (0,0), 1
        for step in range(1, math.ceil(length)):
            current = tuple(math.floor(position[i] + step*delta[i]) for i in range(2))
            if current == previous:
                continue
            x,y = current
            code = (8 if x < previous[0] else 2 if x > previous[0] else 0)
            code |= (1 if y < previous[1] else 4 if y > previous[1] else 0)
            tests = [(x,y)]
            extras = {3:[(x-1,y),(x,y+1)],6:[(x-1,y),(x,y-1)],
                      9:[(x+1,y),(x,y+1)],12:[(x+1,y),(x,y-1)]}
            tests += extras.get(code, [])
            for point in tests:
                expected.append(point)
                if not (0 <= point[0] < 16 and 0 <= point[1] < 16) or point == blocked:
                    result = 0
                    break
            if not result:
                break
            previous = current
        visited.clear()
        length_bits = struct.unpack('<I', struct.pack('<f', length))[0]
        run(0x6f168d30, fine, start, direction, length_bits)
        assert machine.reg_read(UC_X86_REG_EAX) == result, (position, delta, length, blocked)
        assert visited == expected, (position, delta, length, blocked, visited, expected)
        cases += 1
        short_cases += int(length <= 1)
    # Compose actual waypoint selection with normalization, sampling and cells.
    machine.mem_map(0, 4096)
    path, points, self_obj = system + 0x5000, system + 0x6000, system + 0x7000
    write(path + 0x40, points)
    write(path + 0x9c, 0x02000000, self_obj)
    for n in range(5):
        machine.mem_write(points + n*8, struct.pack('<2f', 12.25-n, 8.75))
    machine.mem_write(start, struct.pack('<2f', 8.25, 8.75))
    normalized = {}
    for distance in range(1,5):
        machine.mem_write(direction, struct.pack('<2f', distance, 0))
        machine.reg_write(UC_X86_REG_EDX, direction)
        run(0x6f168280, output)
        length = struct.unpack('<f', machine.mem_read(output, 4))[0]
        unit = struct.unpack('<2f', machine.mem_read(direction, 8))
        assert abs(length-distance) < 0.0001 and abs(unit[0]-1) < 0.0001 and unit[1] == 0
        normalized[distance] = (length, unit)
    waypoint_cases = 0
    for current_index, blocked in itertools.product(range(1,5), [None] + [(x,y) for y in range(16) for x in range(16)]):
        machine.mem_write(cells, clear)
        if blocked is not None:
            write(cells + (blocked[1]*16 + blocked[0])*4, 0x02ffffff)
        write(path + 0x74, current_index)
        write(self_obj + 0x40, 0x20000000)
        write(0, 0x12345678)
        expected_index = current_index - 1
        while expected_index > 0:
            target_x = 12.25 - (expected_index - 1)
            # Use the measured original normalizer outputs, not ideal sqrt/division.
            # Numeric normalization parity is outside this waypoint-selection model.
            length, unit = normalized[int(target_x-8.25)]
            checked = []
            previous = (0,0)
            for step in range(1, math.ceil(length)):
                current = (math.floor(8.25 + step*unit[0]), 8)
                if current != previous:
                    checked.append(current)
                    if previous == (0,0):
                        checked += [(current[0]-1,8),(current[0],7)]
                previous = current
            if blocked in checked:
                break
            expected_index -= 1
        run(0x6f167bf0, path, start)
        assert machine.reg_read(UC_X86_REG_EAX) == expected_index, (current_index, blocked, machine.reg_read(UC_X86_REG_EAX), expected_index)
        assert read(path + 0x74)[0] == current_index
        assert read(self_obj + 0x40)[0] == 0x20000000
        assert read(0)[0] == 0x12345678
        assert read(fine + 0xa4)[0] == 0x02000000
        run(0x6f165e60, path, start, output)
        assert read(path + 0x74)[0] == expected_index
        assert machine.mem_read(output, 8) == machine.mem_read(points + expected_index*8, 8)
        waypoint_cases += 1

    report = dict(binary_sha256=digest, scope=__doc__, passed=True,
                  sampler_cases=cases, waypoint_selection_cases=waypoint_cases, waypoint_commit_cases=waypoint_cases, unchecked_short_segments=short_cases,
                  initial_previous_cell=[0,0], cardinal_normalization=normalized, sampled_parameter='integer k with 1 <= k < length')
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
