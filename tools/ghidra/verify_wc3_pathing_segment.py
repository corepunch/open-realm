#!/usr/bin/env python3
"""Execute the original straight-segment sampler with real footprint/cell checks.

Cardinal unit directions avoid assuming unrecovered normalization parity.
Checks visited-cell order, early rejection and endpoint/short-segment behavior.
Also runs complete waypoint selection and index/point commit for class zero.
The composed model uses measured original normalization outputs, not a
bit-parity replacement normalizer. No stubs or retail bytes included.
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


def segment_cells(config):
    from verify_wc3_pathing_numeric import add, multiply, floor_word, integer_word, bits
    def signed(word):
        return struct.unpack('<i', struct.pack('<I', word))[0]
    length = struct.unpack('<f', struct.pack('<I', config['length']))[0]
    previous, cells = (0, 0), []
    for step in range(1, math.ceil(length)):
        pos = tuple(signed(integer_word(floor_word(add(config['start'][i], multiply(bits(step), config['direction'][i]))))) for i in range(2))
        if pos == previous: continue
        x, y = pos
        code = (8 if x < previous[0] else 2 if x > previous[0] else 0) | (1 if y < previous[1] else 4 if y > previous[1] else 0)
        cls = config['class']; n = cls + 1; offset = n // 2
        x0, y0, x1, y1 = x - offset, y - offset, x - offset + n - 1, y - offset + n - 1
        if not cls:
            tests = [(x,y)] + {3:[(x-1,y),(x,y+1)],6:[(x-1,y),(x,y-1)],
                              9:[(x+1,y),(x,y+1)],12:[(x+1,y),(x,y-1)]}.get(code, [])
        elif code in (1, 4): tests = [(x0 + i, y0 if code == 1 else y1) for i in range(n)]
        elif code in (2, 8): tests = [(x1 if code == 2 else x0, y0 + i) for i in range(n)]
        elif code in (3, 6, 9, 12):
            east, north = code in (3,6), code in (3,9)
            tests = [(x0 - int(east) + i, y0 if north else y1) for i in range(n + 1)]
            tests += [(x1 if east else x0, y0 + (1 if north else -1) + i) for i in range(n)]
        else: tests = [(x0 + i, y0 + j) for j in range(n) for i in range(n)]
        cells += tests; previous = pos
    return cells


def segment_result(config, blocker):
    visited = []
    for point in segment_cells(config):
        visited.append(point)
        x, y = point
        if not (0 <= x < 16 and 0 <= y < 16) or point == blocker:
            return 0, visited
    return 1, visited


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--all-classes', action='store_true', help='oblique/edge/boundary samples across four classes and ground/flight masks')
    parser.add_argument('--engine-library', type=Path, help='compare production C sample result and every queried cell')
    parser.add_argument('--fixture', type=Path, help='freeze original all-class results for asset-free C tests')
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_segment.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_int32)]
        engine.pathing_segment_normalize.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
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
        machine.emu_start(entry, stop, count=2000000)
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
            [] if args.all_classes else [(8.25, 8.75), (0.25, 0.75), (1.25, 1.75)],
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
    fixture_configs, normalizers, engine_queries = [], [], 0
    visit_digest = hashlib.sha256()
    if args.all_classes:
        from verify_wc3_pathing_numeric import bits, square_root, add, multiply, reciprocal
        from generate_wc3_math_tables import reciprocal_table
        table = reciprocal_table()
        deltas = [(1,0),(-1,0),(0,1),(0,-1),(3,0),(-3,0),(0,3),(0,-3),(3,3),(-3,3),(3,-3),(-3,-3),(.75,.5),(-.75,.5),(.5,-.75),(-.5,-.75)]
        normalized_words = []
        for delta in deltas:
            words = list(map(bits, delta))
            length_word = square_root(add(multiply(words[0], words[0]), multiply(words[1], words[1])))
            dir_words = [multiply(w, reciprocal(length_word, table)) for w in words] if length_word > bits(1) else words
            machine.mem_write(direction, struct.pack('<2I', *words))
            machine.reg_write(UC_X86_REG_EDX, direction); run(0x6f168280, output)
            actual = read(output) + read(direction, 2)
            assert actual == [length_word] + dir_words, (delta, actual, length_word, dir_words)
            if engine:
                inp, out = (ctypes.c_uint32 * 2)(*words), (ctypes.c_uint32 * 3)()
                engine.pathing_segment_normalize(inp, out); assert list(out) == actual
            normalizers.append(dict(input=words, result=actual)); normalized_words.append(dir_words)
        for cls, mask, pos, delta, length in itertools.product(range(4), (2,4),
                [(8.25,8.75),(.25,.75),(1.25,1.75),(15.25,15.75),(-.25,.75),(8.999999,8.000001),(8.,8.)],
                normalized_words, (0,.5,1,1.0000001192092896,1.125,2,2.5,5)):
            config = dict(start=list(map(bits,pos)), direction=delta, length=bits(length), **{'class':cls}, mask=mask)
            candidates = sorted({p for p in segment_cells(config) if 0 <= p[0] < 16 and 0 <= p[1] < 16} | {(7,7),(15,15)})
            results = []
            for blocker in [None] + candidates:
                machine.mem_write(cells, clear)
                if blocker is not None: write(cells + (blocker[1]*16 + blocker[0])*4, (mask << 24) | 0xffffff)
                write(0x6fd53a80, cls); write(fine + 0xa4, mask << 24)
                machine.mem_write(start, struct.pack('<2I', *config['start']))
                machine.mem_write(direction, struct.pack('<2I', *delta))
                expected, checked = segment_result(config, blocker)
                visited.clear(); run(0x6f168d30, fine, start, direction, bits(length))
                actual = machine.reg_read(UC_X86_REG_EAX)
                assert actual == expected and visited == checked, (config, blocker, actual, expected, visited, checked)
                if engine:
                    inp = (ctypes.c_uint32 * 9)(*config['start'], *delta, bits(length), cls, 16, 16, mask)
                    cell_bytes = (ctypes.c_uint8 * 256)(); out = (ctypes.c_int32 * 1026)()
                    if blocker is not None: cell_bytes[blocker[1]*16 + blocker[0]] = mask
                    engine.pathing_segment(inp, cell_bytes, out)
                    engine_cells = [tuple(out[2 + 2*i:4 + 2*i]) for i in range(out[1])]
                    assert out[0] == actual and engine_cells == visited, (config, blocker, list(out[:2]), engine_cells, visited)
                    engine_queries += 1
                visit_digest.update(struct.pack('<2I', actual, len(visited)))
                for point in visited: visit_digest.update(struct.pack('<2i', *point))
                results.append(actual); cases += 1; short_cases += int(length <= 1)
            fixture_configs.append(dict(**config, blockers=[list(p) if p else None for p in [None] + candidates], results=results))
        if args.fixture:
            args.fixture.write_text(json.dumps(dict(binary_sha256=digest, dimensions=[16,16], normalizers=normalizers,
                scope='static complete sampler: all classes, oblique/cardinal directions, finite lengths and boundary positions; dynamic eligibility and public admission excluded',
                configs=fixture_configs, visit_digest=visit_digest.hexdigest()), separators=(',', ':')) + '\n')
        write(0x6fd53a80, 0); write(fine + 0xa4, 0x02000000)

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

    route_cases = []
    if args.all_classes:
        fixture_path = Path(__file__).with_name('fixtures') / 'retail-fine-grid-1.27.json'
        routes = json.loads(fixture_path.read_text()); width, height = routes['dimensions']
        write(grid + 0x3c, width, height)
        if engine:
            engine.pathing_segment_waypoint.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint32)]
            engine.pathing_segment_waypoint.restype = ctypes.c_uint32
        for number, case in enumerate(routes['cases']):
            if len(case['path']) < 2: continue
            raw = bytes.fromhex(routes['maps'][case['fixture']])
            machine.mem_write(cells, b''.join(struct.pack('<I', 0x02ffffff if value else 0xffffff) for value in raw))
            route_points = [(x + .5, y + .5) for x,y in reversed(case['path'])]
            route_words = [bits(value) for point in route_points for value in point]
            machine.mem_write(points, struct.pack('<' + 'I' * len(route_words), *route_words))
            source = [bits(value + .5) for value in routes['start']]
            machine.mem_write(start, struct.pack('<2I', *source))
            write(path + 0x74, len(route_points) - 1); write(self_obj + 0x40, 0x20000000)
            write(0x6fd53a80, case['size_class'])
            run(0x6f167bf0, path, start)
            selected = machine.reg_read(UC_X86_REG_EAX)
            assert read(path + 0x74)[0] == len(route_points)-1 and read(self_obj + 0x40)[0] == 0x20000000
            if engine:
                inp = (ctypes.c_uint32 * 7)(*source, case['size_class'], len(route_points)-1, width, height, 2)
                cell_bytes = (ctypes.c_uint8 * len(raw))(*(2 if value else 0 for value in raw))
                point_words = (ctypes.c_uint32 * len(route_words))(*route_words)
                actual = engine.pathing_segment_waypoint(inp, cell_bytes, point_words)
                assert actual == selected, (number, case['size_class'], selected, actual)
            run(0x6f165e60, path, start, output)
            assert read(path + 0x74)[0] == selected and read(output, 2) == route_words[2*selected:2*selected + 2]
            route_cases.append(dict(case=number, selected=selected))
        if args.fixture:
            frozen = json.loads(args.fixture.read_text()); frozen['route_fixture'] = fixture_path.name
            frozen['route_fixture_sha256'] = hashlib.sha256(fixture_path.read_bytes()).hexdigest()
            frozen['route_cases'] = route_cases; args.fixture.write_text(json.dumps(frozen, separators=(',', ':')) + '\n')

    report = dict(binary_sha256=digest, scope=__doc__, passed=True,
                  sampler_cases=cases, route_cases=len(route_cases), engine_route_queries=len(route_cases) if engine else 0, engine_queries=engine_queries, visit_digest=visit_digest.hexdigest(), all_classes=args.all_classes, configs=len(fixture_configs), normalizers=normalizers, waypoint_selection_cases=waypoint_cases, waypoint_commit_cases=waypoint_cases, unchecked_short_segments=short_cases,
                  initial_previous_cell=[0,0], cardinal_normalization=normalized, sampled_parameter='integer k with 1 <= k < length')
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
