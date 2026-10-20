#!/usr/bin/env python3
"""Original separation candidate filter and cooldown primitives.

No stubs; synthetic candidate/query structures and supplied vector locals.
Also executes non-overlap pair accumulation and output damping/clamping slices.
Also executes random overlap directions and their pair accumulation.
Executes rectangle/cell enumeration with preallocated candidate storage.
Executes the full displacement validator with terrain and dynamic objects.
Executes occupancy-bound construction up to the spatial-map mutation call.
Does not execute query bound construction, position resolution, displacement
application or complete crowd movement.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import math
import random
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path, help='compare static endpoint footprint geometry with production C')
    parser.add_argument('--endpoint-fixture', type=Path, help='freeze original endpoint results for asset-free tests')
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_footprint.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8)]
        engine.pathing_footprint.restype = ctypes.c_uint32
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

    shape, mover, separate, query = [system + n for n in (0x1000, 0x2000, 0x3000, 0x4000)]
    write(shape + 0x30, mover, 0)
    write(mover + 0x10, 0x60706375, 0)
    write(mover + 0xac, separate)
    machine.mem_write(mover + 0x90, struct.pack('<f', 1.0))
    write(mover + 0xc0, 0)
    write(query + 0x40, 0)
    write(0x6fd3c744, 0)
    filter_cases = 0
    for category, rank, minimum in itertools.product(range(256), range(16), range(16)):
        write(separate + 0x20, (category << 20) | (rank << 28))
        machine.mem_write(query + 0x50, struct.pack('<HB', category, minimum))
        machine.reg_write(UC_X86_REG_EDX, query)
        run(0x6f16e830, shape)
        assert machine.reg_read(UC_X86_REG_EAX) == int(rank >= minimum)
        filter_cases += 1
    for category in range(256):
        write(separate + 0x20, (category << 20) | 0xf0000000)
        machine.mem_write(query + 0x50, struct.pack('<HB', (category + 1) % 256, 0))
        machine.reg_write(UC_X86_REG_EDX, query)
        run(0x6f16e830, shape)
        assert machine.reg_read(UC_X86_REG_EAX) == 0
        filter_cases += 1
    write(separate + 0x20, 0)
    write(query + 0x50, 0)
    exclusions = [(shape + 0x34, 1 << n) for n in range(24, 32)] + [
        (query + 0x40, mover), (mover + 0x10, 0), (mover + 0x14, 0x80000000),
        (mover + 0x90, 0), (mover + 0x90, 0xbf800000),
        (mover + 0xc0, 0x3f800000), (mover + 0xc0, 0xbf800000), (mover + 0xac, 0)]
    for address, value in exclusions:
        old = read(address)[0]
        write(address, value)
        machine.reg_write(UC_X86_REG_EDX, query)
        run(0x6f16e830, shape)
        assert machine.reg_read(UC_X86_REG_EAX) == 0
        write(address, old)
        filter_cases += 1
    cooldown_cases = 0
    for high, low in itertools.product([0, 0x1234, 0xffff], range(256)):
        write(separate + 0x20, high << 16 | low)
        run(0x6f171320, separate)
        assert machine.reg_read(UC_X86_REG_EAX) == int(low != 0)
        assert read(separate + 0x20)[0] == high << 16 | max(0, low - 1)
        run(0x6f16ebd0, separate)
        assert read(separate + 0x20)[0] == high << 16 | 7
        cooldown_cases += 1
    def floats(address, *values):
        machine.mem_write(address, struct.pack('<' + 'f' * len(values), *values))
    def vector(address):
        return struct.unpack('<2f', machine.mem_read(address, 8))
    floats(0x6fd3c740, -1, 0, 1)
    config, frame = system + 0x5000, stack - 0x1000
    pair_cases, pair_error = 0, 0.0
    for radius, distance, angle, prior in itertools.product([5, 7, 8, 9, 10],
            [0.01, 0.5, 1, 2, 4.9, 5, 6.9, 7, 8, 9, 10, 11], [0, 0.7, 1.6, -2.3], [(0, 0), (0.1, -0.2)]):
        floats(config, radius, 0.01, 0.2, 0.4, 0.7)
        floats(separate + 0x18, *prior)
        dx, dy = distance * math.cos(angle), distance * math.sin(angle)
        floats(frame - 0x68, dx, dy)
        floats(frame - 0x60, 0, 0)
        write(frame - 8, config)
        machine.reg_write(UC_X86_REG_EBP, frame)
        machine.reg_write(UC_X86_REG_ESP, frame - 0x100)
        machine.reg_write(UC_X86_REG_EBX, separate)
        machine.reg_write(UC_X86_REG_ESI, config)
        machine.emu_start(0x6f170359, 0x6f170367, count=10000)
        machine.emu_start(0x6f1703e0, 0x6f170518, count=10000)
        assert machine.reg_read(UC_X86_REG_EIP) == 0x6f170518
        length = 0.4 * max(0, 1 - distance / radius) ** 2
        expected = [prior[0] + length * math.cos(angle), prior[1] + length * math.sin(angle)]
        error = max(abs(a-b) for a,b in zip(vector(separate + 0x18), expected))
        pair_error = max(pair_error, error)
        assert error < 0.0002, (radius, distance, angle, vector(separate + 0x18), expected)
        pair_cases += 1
    tail_cases, tail_error = 0, 0.0
    for magnitude, angle, cap in itertools.product([0, 0.005, 0.02, 0.1, 0.5, 1, 10], [0, 0.7, -2.3], [0.2, 0.5]):
        floats(config, 5, 0.01, cap, 0.4, 0.7)
        floats(separate + 0x18, magnitude * math.cos(angle), magnitude * math.sin(angle))
        write(separate + 0x20, 0xabcd0000)
        write(frame, 0, stop)
        machine.reg_write(UC_X86_REG_EBP, frame)
        machine.reg_write(UC_X86_REG_ESP, frame - 0x7c)
        machine.reg_write(UC_X86_REG_EBX, separate)
        machine.reg_write(UC_X86_REG_ESI, config)
        machine.emu_start(0x6f170525, stop, count=10000)
        assert machine.reg_read(UC_X86_REG_EIP) == stop
        length = min(magnitude * 0.7, cap) if magnitude * 0.7 >= 0.01 else 0
        expected = [length * math.cos(angle), length * math.sin(angle)]
        error = max(abs(a-b) for a,b in zip(vector(separate + 0x18), expected))
        tail_error = max(tail_error, error)
        assert error < 0.0002, (magnitude, angle, cap, vector(separate + 0x18), expected)
        assert read(separate + 0x20)[0] == 0xabcd0000 | (7 if length == 0 else 0)
        tail_cases += 1

    owner, random_out = system + 0x6000, system + 0x6100
    write(0x6fd53a48, owner)
    turn = struct.unpack('<f', machine.mem_read(0x6fcd5464, 4))[0]
    random_cases, direction_error = 0, 0.0
    overlap_cases, overlap_error = 0, 0.0
    for seed in range(64):
        initial = [(seed * 0x10203041) & 0xffffffff, 0]
        write(owner, *initial)
        run(0x6f1b7130, owner)
        word = machine.reg_read(UC_X86_REG_EAX)
        expected_state = read(owner, 2)
        write(owner, *initial)
        machine.reg_write(UC_X86_REG_EDX, owner)
        run(0x6f1d19e0, random_out)
        assert read(owner, 2) == expected_state
        direction = vector(random_out)
        angle = (word & 0x7fffff) / 8388608 * turn
        error = max(abs(a-b) for a,b in zip(direction, [math.cos(angle), math.sin(angle)]))
        direction_error = max(direction_error, error)
        assert error < 0.00002, (seed, direction, angle)
        random_cases += 1
        for radius in [5, 7, 8, 9, 10]:
            for distance in [0, 0.0005]:
                write(owner, *initial)
                floats(config, radius, 0.01, 0.2, 0.4, 0.7)
                floats(separate + 0x18, 0, 0)
                floats(frame - 0x68, distance, 0)
                floats(frame - 0x60, 0, 0)
                write(frame - 8, config)
                machine.reg_write(UC_X86_REG_EBP, frame)
                machine.reg_write(UC_X86_REG_ESP, frame - 0x100)
                machine.reg_write(UC_X86_REG_EBX, separate)
                machine.reg_write(UC_X86_REG_ESI, config)
                machine.emu_start(0x6f170359, 0x6f170367, count=10000)
                machine.emu_start(0x6f1703e0, 0x6f170518, count=10000)
                assert machine.reg_read(UC_X86_REG_EIP) == 0x6f170518
                assert read(owner, 2) == expected_state
                norm = math.hypot(*direction)
                weight = 0.4 * (1 - norm / radius) ** 2
                expected = [value / norm * weight for value in direction]
                error = max(abs(a-b) for a,b in zip(vector(separate + 0x18), expected))
                overlap_error = max(overlap_error, error)
                assert error < 0.0002, (seed, radius, distance, vector(separate + 0x18), expected)
                overlap_cases += 1

    # Execute the full rectangle traversal, cell chains, filter and vector append.
    # Preallocation avoids the unrelated imported allocator; no code is stubbed.
    grid, cells, links, entries = [system + n for n in (0x7000, 0x8000, 0x9000, 0xa000)]
    rect = system + 0xb000
    shapes = [system + 0xc000 + n * 0x200 for n in range(12)]
    rng = random.Random(7085)
    enumeration_cases = 0
    for case in range(1024):
        chains = [[(rng.randrange(4), rng.randrange(12)) for _ in range(rng.randrange(9))]
                  for _ in range(20)]
        # Include excluded sources, dead stamps, rejected filters and counter wrap.
        initial = [0, 100, 0xfffffffd, 0xffffffff][case % 4]
        stamps = [0xffffffff if n == 11 else 0 for n in range(12)]
        source = case % 13
        write(query + 0xc, entries)
        write(query + 0x14, 0, 128, 0)
        write(query + 0x20, shapes[source] if source < 12 else 0)
        write(query + 0x40, 0)
        write(query + 0x50, 0)
        for n, obj in enumerate(shapes):
            unit, sep = obj + 0x40, obj + 0x140
            write(obj + 0x30, unit, 0, stamps[n])
            write(unit + 0x10, 0x60706375, 0)
            floats(unit + 0x90, 1)
            write(unit + 0xac, sep)
            write(unit + 0xc0, 0)
            write(sep + 0x20, (1 << 20) if n % 3 == 0 else 0)
        write(grid + 0x28, cells)
        write(grid + 0x3c, 5)
        write(grid + 0x54, 0, 0, 4, 5)
        write(grid + 0x78, links)
        write(grid + 0xb4, initial)
        index = 0
        for cell, chain in enumerate(chains):
            write(cells + cell * 4, index if chain else 0xffffff)
            for k, (kind, obj) in enumerate(chain):
                next_index = index + 1 if k + 1 < len(chain) else 0xffffff
                write(links + index * 8, kind << 24 | next_index, shapes[obj])
                index += 1
        bounds = [rng.randrange(-2, 7) for _ in range(4)]
        write(rect, *bounds)
        lo0, lo1 = max(0, bounds[0]), max(0, bounds[1])
        hi0, hi1 = min(4, bounds[2]), min(5, bounds[3])
        counter, expected = initial, []
        if lo0 < hi0 and lo1 < hi1:
            counter = (counter + 1) & 0xffffffff
            stamp = counter
            if source < 12:
                stamps[source] = stamp
            for a in range(lo0, hi0):
                for b in range(lo1, hi1):
                    chain = chains[a * 5 + b]
                    if not chain:
                        continue
                    counter = (counter + 1) & 0xffffffff
                    for kind, obj in chain:
                        if kind == 2 or stamps[obj] in (stamp, counter, 0xffffffff):
                            continue
                        if kind == 1:
                            if obj % 3 != 0:
                                expected.append(shapes[obj])
                            stamps[obj] = stamp
                        else:
                            stamps[obj] = counter
        run(0x6f170c00, query, grid, rect)
        count = read(query + 0x1c)[0]
        actual = [read(entries + n * 8, 2) for n in range(count)]
        assert actual == [[obj, 0] for obj in expected], (case, bounds, actual, expected)
        assert read(grid + 0xb4)[0] == counter, case
        assert [read(obj + 0x38)[0] for obj in shapes] == stamps, case
        enumeration_cases += 1

    # Full separation validator, including its scoped self suppression and d4 mode.
    machine.mem_map(0, 4096)  # FS:[0] exception-chain storage in this synthetic process.
    fine, tilemap, terrain, path = [system + n for n in (0x10000, 0x10400, 0x11000, 0x12000)]
    point, self_obj, blocker, occupancy = [system + n for n in (0x12400, 0x12800, 0x12c00, 0x13000)]
    write(owner + 0x24c, fine)
    write(separate + 0x14, mover)
    write(mover + 0xa8, path)
    write(fine + 0x1c, tilemap)
    write(tilemap + 0x28, terrain)
    write(tilemap + 0x3c, 16, 16)
    write(tilemap + 0x78, occupancy)
    clear = struct.pack('<I', 0xffffff) * 256
    validation_cases = 0
    radii = [0.1, 0.499, 0.5, 0.999, 1, 1.499, 1.5, 2]
    positions = [(8.25, 8.75), (0.25, 0.75), (15.25, 15.75), (-0.25, 8.25)]
    blockers = [None] + [(a, b) for a in range(5, 11) for b in range(5, 11)]
    endpoint_results = []
    for radius, (x, y), blocked in itertools.product(radii,
            positions, blockers):
        machine.mem_write(terrain, clear)
        if blocked is not None:
            write(terrain + (blocked[1] * 16 + blocked[0]) * 4, 0x02ffffff)
        floats(point, x, y)
        floats(mover + 0x90, radius)
        write(path + 0x9c, 0x02000000)
        write(mover + 0x98, self_obj)
        write(self_obj + 0x40, 0x20000000)
        write(fine + 0xd0, 0, 7)
        write(0, 0x12345678)
        width = 1 if radius < 0.5 else 2 if radius < 1 else 3 if radius < 1.5 else 4
        offset = width // 2
        covered = [(math.floor(x) - offset + a, math.floor(y) - offset + b)
                   for a in range(width) for b in range(width)]
        expected = int(all(0 <= a < 16 and 0 <= b < 16 and (a, b) != blocked for a, b in covered))
        run(0x6f16ee80, separate, point)
        assert machine.reg_read(UC_X86_REG_EAX) == expected, (radius, x, y, blocked)
        endpoint_results.append(machine.reg_read(UC_X86_REG_EAX))
        if engine:
            words = [struct.unpack('<I', struct.pack('<f', value))[0] for value in (radius, x, y)]
            query = (ctypes.c_uint32 * 6)(*words, 16, 16, 2)
            bitmap = (ctypes.c_uint8 * 256)()
            if blocked is not None:
                bitmap[blocked[1] * 16 + blocked[0]] = 2
            assert engine.pathing_footprint(query, bitmap) == expected, (radius, x, y, blocked)
        assert read(self_obj + 0x40)[0] == 0x20000000
        assert read(fine + 0xd4)[0] == 7
        assert read(fine + 0xa4)[0] == 0x02000000
        assert read(0)[0] == 0x12345678
        validation_cases += 1
    # A 16-world-unit mover occupies class1's biased 2x2 cells. A circle
    # clearance check would reject legal centers on the open side of this wall.
    wall_endpoints = []
    for x in (208.0, 210.5, 223.5, 224.0):
        machine.mem_write(terrain, clear)
        for y in range(16): write(terrain + (y * 16 + 7) * 4, 0x02ffffff)
        floats(point, x / 32.0, 235.75 / 32.0)
        floats(mover + 0x90, 0.5)
        write(path + 0x9c, 0x02000000)
        write(mover + 0x98, self_obj)
        write(self_obj + 0x40, 0x20000000)
        run(0x6f16ee80, separate, point)
        result = machine.reg_read(UC_X86_REG_EAX)
        assert result == int(x < 224.0), (x, result)
        wall_endpoints.append(dict(world_position=[x, 235.75], radius_world=16.0, result=result))
        if engine:
            words = [struct.unpack('<I', struct.pack('<f', v))[0] for v in (0.5, x / 32.0, 235.75 / 32.0)]
            query = (ctypes.c_uint32 * 6)(*words, 16, 16, 2)
            bitmap = (ctypes.c_uint8 * 256)()
            for y in range(16): bitmap[y * 16 + 7] = 2
            assert engine.pathing_footprint(query, bitmap) == result, (x, result)
    dynamic_validation_cases = 0
    for self_present, flags, prior_mode, matches in itertools.product(
            [False, True], [0, 1, 0x10000000, 0x20000000, 0x40000000, 0x60000000, 0x80000000],
            [0, 1, 7], [False, True]):
        machine.mem_write(terrain, clear)
        floats(point, 8.25, 8.75)
        floats(mover + 0x90, 0.25)
        write(path + 0x9c, 1)
        write(mover + 0x98, self_obj if self_present else 0)
        write(self_obj + 0x34, 0x01000001, 0)
        write(self_obj + 0x40, 0)
        write(blocker + 0x34, 0x01000001 if matches else 0x01000002, 0)
        write(blocker + 0x40, flags)
        write(occupancy, 0x01000001, self_obj, 0x01ffffff, blocker)
        write(terrain + (8 * 16 + 8) * 4, 0)
        write(tilemap + 0xb4, 100)
        write(fine + 0xd0, 0, prior_mode)
        expected = int(self_present and (not matches or bool(flags & 0x8fffffff)))
        run(0x6f16ee80, separate, point)
        assert machine.reg_read(UC_X86_REG_EAX) == expected, (self_present, flags, prior_mode, matches)
        assert read(self_obj + 0x40)[0] == 0
        assert read(blocker + 0x40)[0] == flags
        assert read(fine + 0xd4)[0] == prior_mode
        dynamic_validation_cases += 1

    # Bound construction stops at the real spatial-map update, without replacing it.
    proximity_obj, proximity_map = system + 0x13400, system + 0x13800
    write(mover + 0x94, proximity_obj)
    write(proximity_obj + 0x2c, proximity_map)
    write(mover + 0x98, self_obj)
    floats(0x6fd3c74c, 2)
    def f32(value):
        return struct.unpack('<f', struct.pack('<f', value))[0]

    def retail_add(a, b):
        # Finite corpus only: signed doubled significands, arithmetic alignment,
        # then normalization by truncation. This is not IEEE round-to-nearest.
        words = [struct.unpack('<I', struct.pack('<f', v))[0] for v in (a, b)]
        exponents = [(w >> 23) & 255 for w in words]
        if not exponents[0]:
            return f32(b)
        if not exponents[1]:
            return f32(a)
        exponent = max(exponents)
        terms = [(((w & 0x7fffff) | 0x800000) * 2) * (-1 if w >> 31 else 1)
                 for w in words]
        value = sum(term >> (exponent - e) for term, e in zip(terms, exponents))
        if not value:
            return 0.0
        magnitude = abs(value)
        shift = magnitude.bit_length() - 24
        mantissa = magnitude >> shift if shift >= 0 else magnitude << -shift
        word = ((exponent + shift - 1) << 23) | (mantissa & 0x7fffff) | (0x80000000 if value < 0 else 0)
        return struct.unpack('<f', struct.pack('<I', word))[0]

    occupancy_bounds_cases = 0
    for entry, radius, (x, y), scale in itertools.product([0x6f1604d0, 0x6f160590], radii,
            [(8.25, 11.75), (0, 0), (-0.25, -1.75), (16, 8), (127.5, 63.5), (8.999, 11.001)],
            [0.125, 1, 2]):
        floats(mover + 0x90, radius)
        floats(point, x, y)
        floats(proximity_map + 0x68, scale)
        write(self_obj + 0x34, 0x01000001)
        write(stack, stop, mover + 0x90, point)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, mover)
        machine.emu_start(entry, 0x6f14e770, count=10000)
        assert machine.reg_read(UC_X86_REG_EIP) == 0x6f14e770
        arg = read(machine.reg_read(UC_X86_REG_ESP) + 4)[0]
        actual = struct.unpack('<4i', machine.mem_read(arg, 16))
        if entry == 0x6f1604d0:
            expected = tuple(math.floor(f32(retail_add(v, sign*radius)*scale)) + upper
                             for v, sign, upper in [(y,-1,0), (x,-1,0), (y,1,1), (x,1,1)])
            receiver = proximity_obj
        else:
            width = 1 if radius < 0.5 else 2 if radius < 1 else 3 if radius < 1.5 else 4
            offset = width // 2
            expected = (math.floor(y)-offset, math.floor(x)-offset,
                        math.floor(y)-offset+width, math.floor(x)-offset+width)
            receiver = self_obj
        assert actual == expected, (hex(entry), radius, x, y, scale, actual, expected)
        assert machine.reg_read(UC_X86_REG_ECX) == receiver
        occupancy_bounds_cases += 1
    write(self_obj + 0x34, 1)  # Occupancy disabled: static empty rectangle.
    write(stack, stop, mover + 0x90, point)
    machine.reg_write(UC_X86_REG_ESP, stack)
    machine.reg_write(UC_X86_REG_ECX, mover)
    machine.emu_start(0x6f160590, 0x6f14e770, count=10000)
    assert machine.reg_read(UC_X86_REG_EIP) == 0x6f14e770
    assert read(machine.reg_read(UC_X86_REG_ESP) + 4)[0] == 0x6fce4588
    empty_rectangle = struct.unpack('<4i', machine.mem_read(0x6fce4588, 16))
    assert empty_rectangle[0] >= empty_rectangle[2] or empty_rectangle[1] >= empty_rectangle[3]

    report = dict(binary_sha256=digest, scope=__doc__, passed=True, filter_cases=filter_cases,
                  occupancy_bounds_cases=occupancy_bounds_cases, empty_rectangle=empty_rectangle,
                  validation_cases=validation_cases, wall_endpoints=wall_endpoints, dynamic_validation_cases=dynamic_validation_cases,
                  enumeration_cases=enumeration_cases, cooldown_cases=cooldown_cases, pair_cases=pair_cases, pair_max_absolute_error=pair_error,
                  tail_cases=tail_cases, tail_max_absolute_error=tail_error,
                  random_direction_cases=random_cases, direction_max_absolute_error=direction_error,
                  overlap_cases=overlap_cases, overlap_max_absolute_error=overlap_error, turn_constant=turn,
                  overlap_threshold=struct.unpack('<f', machine.mem_read(0x6fcd53a0, 4))[0])
    if engine:
        report.update(engine_endpoint_queries=validation_cases,
                      engine_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest())
    if args.endpoint_fixture:
        word = lambda value: struct.unpack('<I', struct.pack('<f', value))[0]
        fixture = dict(binary_sha256=digest, dimensions=[16, 16], query=2,
                       radius_words=[word(value) for value in radii],
                       position_words=[[word(value) for value in pos] for pos in positions],
                       blockers=blockers, results=endpoint_results, wall_endpoints=wall_endpoints,
                       scope='complete original 16ee80 static terrain endpoints; dynamic self-suppression is separate')
        args.endpoint_fixture.parent.mkdir(parents=True, exist_ok=True)
        args.endpoint_fixture.write_text(json.dumps(fixture, indent=2) + '\n')
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
