#!/usr/bin/env python3
"""Execute retail spatial rectangle mutation and its real cell-link writers.

Synthetic 8x8 maps with preallocated link capacity; no stubs or imported
allocator. Checks lazy removal records, chain order, counters and dirty bits, then
consumes the resulting chains with the full original separation query.
Also executes dirty-cell and full-map cleanup, checks stable query order and reuses reclaimed links.
Contains no retail bytes; requires the hash-matched local DLL and Unicorn.
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
    parser.add_argument('--engine-library', type=Path)
    args = parser.parse_args()
    engine = None
    if args.engine_library:
        engine = ctypes.CDLL(str(args.engine_library.resolve()))
        engine.pathing_spatial_update.argtypes = [ctypes.c_uint, ctypes.POINTER(ctypes.c_int)]
        engine.pathing_spatial_cell.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_uint)]
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

    grid, cells, dirty, links = [system + n for n in (0x1000, 0x2000, 0x3000, 0x4000)]
    objects = [system + 0x100 + n * 0x100 for n in range(3)]
    rect = system + 0x1800
    query, query_entries, query_rect = system + 0x19000, system + 0x19400, system + 0x19800
    write(query + 0xc, query_entries)
    write(query + 0x14, 0, 16, 0)
    write(query + 0x20, 0)
    write(query + 0x40, 0)
    write(query + 0x50, 0)
    write(0x6fd3c744, 0)
    for n, obj in enumerate(objects):
        mover = system + 0x18000 + n * 0x200
        write(obj + 0x30, mover, 0, 0)
        write(mover + 0x10, 0x60706375, 0)
        write(mover + 0x90, 0x3f800000)
        write(mover + 0xac, mover + 0x100)
        write(mover + 0xc0, 0)
        write(mover + 0x120, 0)
    empty = (-128000,) * 4
    rng = random.Random(7085)
    cases, removal_records, insertion_records = 0, 0, 0
    cleanup_cases, reclaimed_records = 0, 0
    dirty_cleanup_cases, full_sweep_cases = 0, 0

    def covered(bounds):
        y0, x0, y1, x1 = bounds
        return {y * 8 + x for y in range(max(0,y0), min(8,y1))
                for x in range(max(0,x0), min(8,x1))}

    for seed in range(32):
        if engine:
            engine.pathing_spatial_clear()
        free_count = 32 if seed % 2 else 0
        write(grid + 0x28, cells)
        write(grid + 0x38, 64, 8, 8)
        write(grid + 0x54, 0, 0, 8, 8)
        write(grid + 0x78, links)
        write(grid + 0x80, 0, 8192, free_count)
        write(grid + 0x98, dirty)
        write(grid + 0xa8, 2)
        write(grid + 0xac, 0 if free_count else 0xffffff, 0)
        for index in range(free_count):
            write(links + index * 8, index + 1 if index + 1 < free_count else 0xffffff, 0)
        write(dirty, 0, 0)
        terrain = [((cell % 4) << 24) for cell in range(64)]
        for cell in range(64):
            write(cells + cell * 4, terrain[cell] | 0xffffff)
        bounds = [empty] * 3
        object_counts = [0] * 3
        chains = [[] for _ in range(64)]
        dirty_cells = set()
        count = 0
        high_water = free_count
        write(grid + 0xb4, 10000 + seed * 10000)
        for obj in objects:
            write(obj + 0x1c, *empty)
            write(obj + 0x2c, grid)
            write(obj + 0x3c, 0)
        for step in range(40):
            selected = rng.randrange(3)
            obj = objects[selected]
            y, x = rng.randrange(-2, 10), rng.randrange(-2, 10)
            new = (y, x, y + rng.randrange(1, 5), x + rng.randrange(1, 5))
            if step % 7 == 0:
                new = bounds[selected]
            elif step % 11 == 0:
                new = empty
            old_cells, new_cells = covered(bounds[selected]), covered(new)
            removed, added = old_cells - new_cells, new_cells - old_cells
            for cell in removed:
                chains[cell].insert(0, (0, obj))
            for cell in added:
                chains[cell].insert(0, (1, obj))
            dirty_cells |= removed
            count += len(removed) + len(added)
            high_water = max(high_water, count)
            object_counts[selected] += len(removed) + len(added)
            removal_records += len(removed)
            insertion_records += len(added)
            write(rect, *new)
            run(0x6f14e770, obj, rect)
            if engine:
                assert engine.pathing_spatial_update(selected, (ctypes.c_int * 4)(*new)) == 1
            bounds[selected] = new
            assert read(obj + 0x1c, 4) == [v & 0xffffffff for v in new]
            assert read(grid + 0xb0)[0] == count
            assert read(grid + 0x88)[0] == high_water
            for n, address in enumerate(objects):
                assert read(address + 0x3c)[0] == object_counts[n]
            bitmap = read(dirty, 2)
            assert {n for n in range(64) if bitmap[n//32] & (1 << (n%32))} == dirty_cells
            for cell in range(64):
                head = read(cells + cell * 4)[0]
                assert head & 0xff000000 == terrain[cell]
                index, actual, visited = head & 0xffffff, [], set()
                while index != 0xffffff:
                    assert index not in visited and index < high_water
                    visited.add(index)
                    word, payload = read(links + index * 8, 2)
                    actual.append((word >> 24, payload))
                    index = word & 0xffffff
                assert actual == chains[cell], (seed, step, cell, actual, chains[cell])
                # Newest record for an object controls effective membership.
                active, seen, ordered = set(), set(), []
                for kind, payload in actual:
                    if payload not in seen:
                        seen.add(payload)
                        if kind == 1:
                            active.add(payload)
                            ordered.append(objects.index(payload))
                expected = {objects[n] for n in range(3) if cell in covered(bounds[n])}
                assert active == expected
                if engine:
                    out = (ctypes.c_uint * 3)()
                    amount = engine.pathing_spatial_cell(cell % 8, cell // 8, out)
                    assert list(out)[:amount] == ordered, ('engine active order', seed, step, cell)
            # Consume actual lazy chains with the original separation query.
            # This links mutation evidence to its real downstream reader.
            query_bounds = (0, 0, 8, 8) if step % 2 else (1, 2, 6, 7)
            write(query_rect, *query_bounds)
            write(query + 0x1c, 0)
            run(0x6f170c00, query, grid, query_rect)
            candidate_count = read(query + 0x1c)[0]
            candidates = [read(query_entries + n * 8)[0] for n in range(candidate_count)]
            expected_candidates = {objects[n] for n in range(3)
                                   if covered(bounds[n]) & covered(query_bounds)}
            assert len(candidates) == len(set(candidates))
            assert set(candidates) == expected_candidates, (seed, step, candidates, expected_candidates)
            if step % 8 == 7:
                # Full dirty-cell cleanup, then the same real separation query.
                before_count = count
                full_sweep = step % 16 == 15
                full_sweep_cases += int(full_sweep)
                dirty_cleanup_cases += int(not full_sweep)
                run(0x6f14dfc0 if full_sweep else 0x6f14df20, grid)
                count = 0
                object_counts = [0] * 3
                for cell in range(64):
                    if full_sweep or cell in dirty_cells:
                        kept, seen = [], set()
                        for kind, payload in chains[cell]:
                            if payload not in seen and kind == 1:
                                kept.append((kind, payload))
                            seen.add(payload)
                        chains[cell] = kept
                    count += len(chains[cell])
                    for _, payload in chains[cell]:
                        object_counts[objects.index(payload)] += 1
                    index = read(cells + cell * 4)[0] & 0xffffff
                    actual = []
                    while index != 0xffffff:
                        word, payload = read(links + index * 8, 2)
                        actual.append((word >> 24, payload))
                        index = word & 0xffffff
                    assert actual == chains[cell], ('cleanup', seed, step, cell)
                reclaimed_records += before_count - count
                if not full_sweep:
                    dirty_cells.clear()
                bitmap = read(dirty, 2)
                assert {n for n in range(64) if bitmap[n//32] & (1 << (n%32))} == dirty_cells
                assert read(grid + 0xb0)[0] == count
                assert read(grid + 0x88)[0] == high_water
                for n, address in enumerate(objects):
                    assert read(address + 0x3c)[0] == object_counts[n]
                free_index = read(grid + 0xac)[0]
                free_seen = set()
                while free_index != 0xffffff:
                    assert free_index not in free_seen and free_index < high_water
                    free_seen.add(free_index)
                    free_index = read(links + free_index * 8)[0] & 0xffffff
                assert len(free_seen) == high_water - count
                write(query + 0x1c, 0)
                run(0x6f170c00, query, grid, query_rect)
                after = [read(query_entries + n * 8)[0] for n in range(read(query + 0x1c)[0])]
                assert after == candidates, ('cleanup changed candidate order', seed, step)
                cleanup_cases += 1
            cases += 1
    report = dict(binary_sha256=digest, scope=__doc__, passed=True,
                  rectangle_updates=cases, separation_queries=cases + cleanup_cases,
                  cleanup_cases=cleanup_cases, dirty_cleanup_cases=dirty_cleanup_cases, full_sweep_cases=full_sweep_cases,
                  reclaimed_records=reclaimed_records, insertion_records=insertion_records,
                  removal_records=removal_records, sequences=32,
                  free_list_sequences=16, initially_empty_vector_sequences=16,
                  engine_active_chain_comparisons=cases * 64 if engine else 0,
                  engine_library_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest() if engine else None)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
