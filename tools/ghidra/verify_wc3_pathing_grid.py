#!/usr/bin/env python3
"""Run original fine-grid searches against independent Dijkstra graph costs.

No code stubs: retail node creation, per-cell stamps, occupancy, expansion,
heap, relaxation and loop execute. Search/map storage is initialized directly;
this does not validate constructors, request admission or route smoothing.
"""
import argparse
import ctypes
import hashlib
import heapq
import json
import random
import struct
from pathlib import Path
from verify_wc3_pathing_footprints import CLASSES, DIRECTIONS, perimeter


def footprint_graph(blocked, dimensions, size_class):
    """Recovered legal graph, independent of the C search's queue/heuristic."""
    width, height = dimensions
    _, offset, ring_width, masks = CLASSES[size_class]
    edges = []
    for y in range(height):
        for x in range(width):
            bits = sum(1 << i for i, p in enumerate(perimeter(x, y, offset, ring_width))
                       if not (0 <= p[0] < width and 0 <= p[1] < height) or p in blocked)
            edges.append(sum(1 << d for d, ((dx, dy), mask) in enumerate(zip(DIRECTIONS, masks))
                             if not bits & mask and 0 <= x + dx < width and 0 <= y + dy < height))
    return bytes(edges)


def reference(blocked, width, height, start, goal, size_class):
    _, offset, ring_width, masks = CLASSES[size_class]
    costs, pending = {start: 0}, [(0, start)]
    while pending:
        cost, point = heapq.heappop(pending)
        if cost != costs[point]:
            continue
        if point == goal:
            return cost, costs
        x, y = point
        bits = sum(1 << i for i, p in enumerate(perimeter(x, y, offset, ring_width))
                   if not (0 <= p[0] < width and 0 <= p[1] < height) or p in blocked)
        for (dx, dy), mask in zip(DIRECTIONS, masks):
            other = x + dx, y + dy
            if bits & mask or not (0 <= other[0] < width and 0 <= other[1] < height):
                continue
            value = cost + (21 if dx and dy else 15)
            if value < costs.get(other, 1 << 60):
                costs[other] = value
                heapq.heappush(pending, (value, other))
    return None, costs


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, help='freeze original cell routes for asset-free engine comparisons')
    parser.add_argument('--engine-library', type=Path, help='compare production C cell routes, work and node creation')
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_fine_grid.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint8),
                                            ctypes.POINTER(ctypes.c_int32)]
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

    machine.mem_map(0x10000000, 0x200000)
    machine.mem_map(0x20000000, 0x10000)
    system, tilemap, cells, bitmap = 0x10000000, 0x10000200, 0x10001000, 0x10002000
    nodes, links, heap = 0x10010000, 0x10020000, 0x10030000
    stack, stop = 0x20008000, 0x30000000
    route, route_data = 0x10100000, 0x10101000
    source_ptr, target_ptr, mask_ptr, radius_ptr = 0x10000400, 0x10000410, 0x10000420, 0x10000430
    for address, value in ((0x6fd3c740, -1.0), (0x6fd3c744, 0.0), (0x6fd3c748, 1.0)):
        machine.mem_write(address, struct.pack('<f', value))
    width = height = 24
    start, goal = (4, 4), (19, 19)

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
            raise RuntimeError('retail grid search exceeded instruction budget')
        return machine.reg_read(UC_X86_REG_EAX)

    rng = random.Random(12717085)
    fixtures = [('open', set()), ('solid_wall', {(12, y) for y in range(height)})]
    fixtures += [(f'gap_{gap}', {(12, y) for y in range(height) if not 10 <= y < 10 + gap}) for gap in range(1, 7)]
    for index in range(64):
        density = (.08, .16, .24, .32)[index % 4]
        blocked = {(x, y) for y in range(height) for x in range(width) if rng.random() < density}
        for cx, cy in (start, goal):
            blocked.difference_update((x, y) for x in range(cx - 3, cx + 4) for y in range(cy - 3, cy + 4))
        fixtures.append((f'random_{index}', blocked))
    records, edge_cases, engine_cases = [], [], []
    for name, blocked in fixtures:
        for size_class in range(4):
            machine.mem_write(system, bytes(0x400))
            machine.mem_write(bitmap, bytes(128))
            machine.mem_write(cells, b''.join(struct.pack('<I', 0x02ffffff if (x, y) in blocked else 0x00ffffff)
                                            for y in range(height) for x in range(width)))
            write(system + 0x1c, tilemap, 1)
            write(system + 0x30, nodes)
            write(system + 0x3c, 1024, 0)
            write(system + 0x50, heap)
            write(system + 0x5c, 32768, 1, -3, 100000, 0)
            write(system + 0x80, *start, *goal)
            write(system + 0x98, sum((a - b) ** 2 for a, b in zip(start, goal)), 0)
            write(system + 0xa0, size_class, 0x02000000)
            write(tilemap + 0x28, cells)
            write(tilemap + 0x3c, width, height)
            write(tilemap + 0x78, links)
            write(tilemap + 0x84, 1024, 0)
            write(tilemap + 0x98, bitmap)
            write(tilemap + 0xac, 0xffffff)
            start_index = run(0x6f147af0, system, *start)
            goal_index = run(0x6f147af0, system, *goal)
            write(system + 0x90, start_index, goal_index)
            result = run(0x6f14aa10, system)
            expected, reachable = reference(blocked, width, height, start, goal, size_class)
            actual = None if result == 0xffffffff else read(nodes + result * 36 + 0x14)[0]
            if (result != 0xffffffff and result != goal_index) or actual != expected:
                raise RuntimeError(f'grid mismatch {name} class={size_class} result={result} cost={actual} wanted={expected}')
            chain, points = [], []
            if result != 0xffffffff:
                index = result
                while index != 0xffffffff:
                    if index in chain:
                        raise RuntimeError('cyclic retail parent chain')
                    chain.append(index)
                    index = read(nodes + index * 36 + 0x1c)[0]
                if chain[-1] != start_index:
                    raise RuntimeError('retail parent chain does not reach start')
                points = [tuple(read(nodes + index * 36, 2)) for index in reversed(chain)]
                reconstructed_cost = 0
                _, offset, ring_width, masks = CLASSES[size_class]
                for a, b in zip(points, points[1:]):
                    delta = b[0] - a[0], b[1] - a[1]
                    mask = masks[DIRECTIONS.index(delta)]
                    bits = sum(1 << i for i, p in enumerate(perimeter(*a, offset, ring_width))
                               if not (0 <= p[0] < width and 0 <= p[1] < height) or p in blocked)
                    if bits & mask:
                        raise RuntimeError('retail chain contains forbidden footprint edge')
                    reconstructed_cost += 21 if all(delta) else 15
                if reconstructed_cost != actual:
                    raise RuntimeError('retail parent chain cost differs from goal g')
            else:
                nearest = min(sum((a - b) ** 2 for a, b in zip(p, goal)) for p in reachable)
                if read(system + 0x98)[0] != nearest:
                    raise RuntimeError('failed search nearest-distance mismatch')
            first_pops, first_nodes = read(system + 0x6c)[0], read(system + 0x40)[0]
            if engine:
                graph = footprint_graph(blocked, (width, height), size_class)
                edges = (ctypes.c_uint8 * len(graph)).from_buffer_copy(graph)
                query = (ctypes.c_uint32 * 7)(width, height, *start, *goal, 2048)
                wanted = [actual if actual is not None else -1, first_pops, first_nodes, len(points)]
                for _ in range(2):
                    output = (ctypes.c_int32 * (6 + 2 * 16386))()
                    engine.pathing_fine_grid(query, edges, output)
                    path = [[output[6 + i * 2], output[7 + i * 2]] for i in range(output[3])]
                    if list(output[:4]) != wanted or path != [list(p) for p in points]:
                        raise RuntimeError(f'production C fine-search mismatch: {name} class={size_class}')
            link_count = read(tilemap + 0x88)[0]
            node_bytes = bytes(machine.mem_read(nodes, first_nodes * 36))
            run(0x6f14a980, system, 0)
            write(system + 0x20, 2)
            write(system + 0x98, sum((a - b) ** 2 for a, b in zip(start, goal)), 0)
            write(system + 0xcc, 0, 0, 0)
            start_index = run(0x6f147af0, system, *start)
            goal_index = run(0x6f147af0, system, *goal)
            write(system + 0x90, start_index, goal_index)
            repeated = run(0x6f14aa10, system)
            if (repeated != result or read(system + 0x6c)[0] != first_pops or
                read(system + 0x40)[0] != first_nodes or read(tilemap + 0x88)[0] != link_count or
                bytes(machine.mem_read(nodes, first_nodes * 36)) != node_bytes):
                raise RuntimeError('repeated search with retained per-cell metadata differs')
            # Execute the real setup/search/reconstruction request as well.
            source = start[0] + .25, start[1] + .75
            target = goal[0] + .25, goal[1] + .75
            machine.mem_write(source_ptr, struct.pack('<ff', *source))
            machine.mem_write(target_ptr, struct.pack('<ff', *target))
            machine.mem_write(radius_ptr, struct.pack('<f', .25 + .5 * size_class))
            write(mask_ptr, 0x02000000)
            write(route + 0xc, route_data)
            write(route + 0x18, 1024, 0)
            request_result = run(0x6f148100, system, route, source_ptr, target_ptr, mask_ptr, 100000, radius_ptr, 0)
            route_count = read(route + 0x1c)[0]
            if not 1 <= route_count <= 1024:
                raise RuntimeError('request produced invalid route length')
            route_points = [struct.unpack('<ff', machine.mem_read(route_data + i * 8, 8)) for i in range(route_count)]
            nearest_index = read(system + 0x9c)[0]
            nearest_xy = read(nodes + nearest_index * 36, 2)
            wanted_end = target if expected is not None else source if nearest_index == 0 else tuple(v + .5 for v in nearest_xy)
            adjusted_target = struct.unpack('<ff', machine.mem_read(system + 0x78, 8))
            if (request_result != int(expected is not None) or route_points[0] != wanted_end or route_points[-1] != source or
                read(system + 0xa0)[0] & 0xffff != size_class or read(system + 0x6c)[0] != first_pops):
                raise RuntimeError('setup/search/reconstruction request disagrees with core search')
            if expected is None and nearest_index != 0 and adjusted_target != wanted_end:
                raise RuntimeError('partial route did not update stored adjusted destination')
            records.append(dict(fixture=name, size_class=size_class, cost=actual,
                                pops=read(system + 0x6c)[0], nodes=read(system + 0x40)[0], chain_length=len(chain),
                                request_result=request_result, route_points=route_count, route_end=route_points[0]))
            if args.fixture:
                engine_cases.append(dict(fixture=name, size_class=size_class, cost=actual, pops=first_pops,
                                         nodes=first_nodes, path=points))
            if name == 'open':
                for label, destination, budget, wanted_result, wanted_pops in (
                        ('same_cell', (start[0] + .875, start[1] + .125), 100000, 1, 0),
                        ('zero_budget', target, 0, 0, 1)):
                    machine.mem_write(target_ptr, struct.pack('<ff', *destination))
                    value = run(0x6f148100, system, route, source_ptr, target_ptr, mask_ptr, budget, radius_ptr, 0)
                    count = read(route + 0x1c)[0]
                    point = struct.unpack('<ff', machine.mem_read(route_data, 8))
                    if (value != wanted_result or count != 1 or point != (destination if wanted_result else source) or
                        read(system + 0x6c)[0] != wanted_pops):
                        raise RuntimeError(f'request edge case failed: {label}')
                    edge_cases.append(dict(case=label, size_class=size_class, result=value, pops=wanted_pops, point=point))
        if len(records) % 64 == 0:
            print(f'{len(records)} retail footprint searches and repeats checked', flush=True)
    report = dict(binary_sha256=digest, cases=len(records), repeated_searches=len(records), complete_requests=len(records), request_edge_cases=edge_cases, mismatches=[],
                  reached=sum(r['cost'] is not None for r in records), exhausted=sum(r['cost'] is None for r in records),
                  seed=12717085, dimensions=[width, height], start=start, goal=goal,
                  scope='original core loop and full setup/search/reconstruction request; allocation/reset/stamp reuse; direct initialized storage and -1/0/1 runtime constants; static terrain only; Dijkstra reference uses recovered footprint graph; no path-owned admission or smoothing',
                  searches=records)
    if engine:
        report.update(engine_queries=len(records), engine_repeats=len(records),
                      engine_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest())
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    if args.fixture:
        fixture = dict(binary_sha256=digest, seed=12717085, dimensions=[width, height], start=start, goal=goal,
                       scope='original static fine-search cell route, cost, pops and allocated nodes; excludes admission and smoothing',
                       maps={name: bytes(int((x, y) in blocked) for y in range(height) for x in range(width)).hex()
                             for name, blocked in fixtures}, cases=engine_cases)
        args.fixture.parent.mkdir(parents=True, exist_ok=True)
        args.fixture.write_text(json.dumps(fixture, separators=(',', ':')) + '\n')
    print(f'{len(records)} full retail fine-grid searches plus {len(records)} stamp-reuse repeats and {len(records)} complete requests; all pass')


if __name__ == '__main__':
    main()
