#!/usr/bin/env python3
"""Check original fine-search perimeter scans and four neighbour-mask consumers.

Only node allocation is stubbed in composed terrain cases. Mask-only cases also
stub the perimeter reader, isolating every possible blocked-ring bit pattern.
Requires Unicorn and a local hash-matched DLL; contains no retail bytes.
"""
import argparse
import hashlib
import itertools
import json
import struct
import time
from pathlib import Path

CLASSES = [
    (0x6f14b890, 1, 3, [0x83, 2, 0xe, 0x80, 8, 0xe0, 0x20, 0x38]),
    (0x6f14b9b0, 2, 4, [0xc07, 6, 0x3e, 0xc00, 0x30, 0xf80, 0x180, 0x1f0]),
    (0x6f14bae0, 2, 5, [0xe00f, 0xe, 0xfe, 0xe000, 0xe0, 0xfe00, 0xe00, 0xfe0]),
    (0x6f14bc10, 3, 6, [0xf001f, 0x1e, 0x3fe, 0xf0000, 0x3c0, 0xff800, 0x7800, 0x7fc0]),
]
DIRECTIONS = [(-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1)]


def perimeter(x, y, offset, width):
    lo_x, lo_y = x - offset, y - offset
    hi_x, hi_y = lo_x + width - 1, lo_y + width - 1
    return ([(i, lo_y) for i in range(lo_x, hi_x + 1)] +
            [(hi_x, i) for i in range(lo_y + 1, hi_y + 1)] +
            [(i, hi_y) for i in range(hi_x - 1, lo_x - 1, -1)] +
            [(lo_x, i) for i in range(hi_y - 1, lo_y, -1)])


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--exhaustive', action='store_true', help='all 1,118,464 mask combinations; may take minutes')
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

    machine.mem_map(0x10000000, 0x10000)
    machine.mem_map(0x20000000, 0x10000)
    system, tilemap, cells = 0x10000000, 0x10000200, 0x10001000
    point, output, stack, stop = 0x10000400, 0x10000500, 0x20008000, 0x30000000
    mask_only, supplied_mask = False, 0
    mask_cases, terrain_cases, failures = 0, 0, []
    started = time.monotonic()

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def finish(uc, result, argc):
        sp = uc.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack('<I', uc.mem_read(sp, 4))[0]
        uc.reg_write(UC_X86_REG_EAX, result & 0xffffffff)
        uc.reg_write(UC_X86_REG_ESP, sp + 4 * (argc + 1))
        uc.reg_write(UC_X86_REG_EIP, ret)

    def node(uc, address, size, data):
        sp = uc.reg_read(UC_X86_REG_ESP)
        x, y = struct.unpack('<ii', uc.mem_read(sp + 4, 8))
        finish(uc, ((y + 8) * 64 + x + 8), 2)

    def ring(uc, address, size, data):
        if mask_only:
            finish(uc, supplied_mask, 4)

    machine.hook_add(UC_HOOK_CODE, node, begin=0x6f147af0, end=0x6f147af0)
    machine.hook_add(UC_HOOK_CODE, ring, begin=0x6f148d00, end=0x6f148d00)
    write(system + 0x1c, tilemap)
    write(system + 0xa4, 0x02000000)
    write(tilemap + 0x28, cells)
    write(tilemap + 0x3c, 16, 16)
    clear = struct.pack('<I', 0x00ffffff) * 256

    def verify(entry, masks, x, y, blocked, mode):
        write(point, x, y)
        write(output, *([0xdeadbeef] * 8))
        write(stack, stop, point, output)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, system)
        machine.emu_start(entry, stop, count=10000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail expansion exceeded instruction budget')
        actual = list(struct.unpack('<8I', machine.mem_read(output, 32)))
        expected = [0xffffffff if blocked & mask else (y + dy + 8) * 64 + x + dx + 8
                    for mask, (dx, dy) in zip(masks, DIRECTIONS)]
        if actual != expected:
            failures.append(dict(function=hex(entry), mode=mode, point=[x, y], mask=blocked,
                                 actual=actual, expected=expected))
            if len(failures) >= 20:
                raise RuntimeError('20 footprint mismatches')

    for class_id, (entry, offset, width, masks) in enumerate(CLASSES):
        bits = 4 * (width - 1)
        basis = [0, (1 << bits) - 1] + [1 << i for i in range(bits)]
        basis += [(1 << i) | (1 << j) for i, j in itertools.combinations(range(bits), 2)]
        mask_only = True
        for supplied_mask in range(1 << bits) if args.exhaustive else basis:
            verify(entry, masks, 8, 8, supplied_mask, 'mask')
            mask_cases += 1
            if mask_cases % 65536 == 0:
                print(f'{mask_cases} masks checked; {time.monotonic() - started:.1f}s', flush=True)
        mask_only = False
        for x, y in ((8, 8), (0, 0), (15, 15), (0, 8), (15, 8), (8, 0), (8, 15)):
            ring_cells = perimeter(x, y, offset, width)
            for mask in basis:
                machine.mem_write(cells, clear)
                effective = 0
                for bit, (cx, cy) in enumerate(ring_cells):
                    inside = 0 <= cx < 16 and 0 <= cy < 16
                    if not inside or mask & (1 << bit):
                        effective |= 1 << bit
                        if inside:
                            write(cells + (cy * 16 + cx) * 4, 0x02ffffff)
                verify(entry, masks, x, y, effective, 'terrain')
                terrain_cases += 1
        print(f'class {class_id} complete; {mask_cases} mask / {terrain_cases} terrain cases', flush=True)
    # Exercise the real cell predicate independently of the perimeter consumer.
    links, obj = 0x10000600, 0x10000700
    write(tilemap + 0x78, links)
    occupancy_cases = 0

    def cell_result():
        write(stack, stop, 8, 8)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, system)
        machine.emu_start(0x6f1489a0, stop, count=2000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail cell predicate exceeded instruction budget')
        return machine.reg_read(UC_X86_REG_EAX)

    for cell_flags, query_flags in itertools.product(range(256), repeat=2):
        write(cells + (8 * 16 + 8) * 4, (cell_flags << 24) | 0xffffff)
        write(system + 0xa4, query_flags << 24)
        write(system + 0xd0, 0)
        actual = cell_result()
        expected = int(not (cell_flags & query_flags))
        blocked = struct.unpack('<I', machine.mem_read(system + 0xd0, 4))[0]
        occupancy_cases += 1
        if actual != expected or blocked != 1 - expected:
            raise RuntimeError('terrain high-byte predicate mismatch')

    write(cells + (8 * 16 + 8) * 4, 0)  # first object-list link, no terrain flags
    write(system + 0xa4, 1)
    flags = [0] + [1 << bit for bit in range(32)]
    for kind, active, disabled, overlap, target, query_mode, policy in itertools.product(
            range(4), range(2), range(2), range(2), range(2), range(2), flags):
        write(links, (kind << 24) | 0xffffff, obj)
        write(obj + 0x34, (active << 24) | (1 if overlap else 2), -1 if disabled else 0)
        write(obj + 0x40, policy)
        write(tilemap + 0xb4, 7)
        write(system + 0xa8, obj if target else 0)
        write(system + 0xcc, 0, 0, query_mode)
        actual = cell_result()
        visited = kind != 2 and active and not disabled
        special = int(visited and kind == 1 and target)
        denied = bool(visited and kind == 1 and not (policy & 0x8fffffff) and
                      (query_mode or not (policy & 0x60000000)) and overlap)
        cc, d0 = struct.unpack('<II', machine.mem_read(system + 0xcc, 8))
        stamp = struct.unpack('<I', machine.mem_read(obj + 0x38, 4))[0]
        expected_stamp = 0xffffffff if disabled else 8 if visited else 0
        occupancy_cases += 1
        if actual != int(not denied) or cc != special or d0 != int(denied) or stamp != expected_stamp:
            failures.append(dict(kind=kind, active=active, disabled=disabled, overlap=overlap,
                                 target=target, query_mode=query_mode, policy=policy,
                                 actual=[actual, cc, d0, stamp],
                                 expected=[int(not denied), special, int(denied), expected_stamp]))
            if len(failures) >= 20:
                raise RuntimeError('20 dynamic occupancy mismatches')

    report = dict(binary_sha256=digest, mask_cases=mask_cases, terrain_cases=terrain_cases,
                  occupancy_cases=occupancy_cases, exhaustive_masks=args.exhaustive, mismatches=failures,
                  scope='mask consumers; composed terrain rings/bounds; high-byte and single-link dynamic predicate; excludes multi-link chains, allocation and initial footprint validity',
                  classes=[dict(function=hex(e), offset=o, width=w, masks=m) for e, o, w, m in CLASSES])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{mask_cases + terrain_cases + occupancy_cases} retail footprint cases; {len(failures)} mismatches')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
