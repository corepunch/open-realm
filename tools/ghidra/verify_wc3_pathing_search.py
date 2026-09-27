#!/usr/bin/env python3
"""Recover fine-search heuristic by executing original relaxation instructions.

Fresh-node cases execute retail arithmetic and bookkeeping, stubbing enqueue.
No retail bytes are distributed; requires Unicorn and the matching local DLL.
"""
import argparse
import hashlib
import itertools
import json
import struct
from pathlib import Path


def heuristic(dx, dy):
    a, b = sorted((15 * abs(dx), 15 * abs(dy)), reverse=True)
    if (a >> 2) < b:
        t = b + (b >> 1)
        return a + (a >> 6) - (a >> 4) + (t >> 2) + (t >> 7)
    return a


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX
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

    machine.mem_map(0x10000000, 0x10000)
    machine.mem_map(0x20000000, 0x10000)
    system, nodes, stack, stop = 0x10000000, 0x10001000, 0x20008000, 0x30000000
    queued = []

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def enqueue(uc, address, size, data):
        sp = uc.reg_read(UC_X86_REG_ESP)
        ret, index = struct.unpack('<II', uc.mem_read(sp, 8))
        queued.append(index)
        uc.reg_write(UC_X86_REG_ESP, sp + 8)
        uc.reg_write(UC_X86_REG_EIP, ret)

    machine.hook_add(UC_HOOK_CODE, enqueue, begin=0x6f147c30, end=0x6f147c30)
    write(system + 0x30, nodes)
    write(system + 0x88, 0, 0)
    write(nodes + 0x24 + 0x14, 100)
    results, failures, overestimates = {}, [], []
    for dx, dy in itertools.product(range(-256, 257), repeat=2):
        write(nodes, dx, dy, 0, -1, -1, 0, 0, -1)
        write(system + 0x98, -1, -1)
        write(stack, stop, 0, 1, 21)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, system)
        queued.clear()
        machine.emu_start(0x6f14a560, stop, count=2000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail relaxation exceeded instruction budget')
        g, h, parent = struct.unpack('<III', machine.mem_read(nodes + 0x14, 12))
        nearest, nearest_index = struct.unpack('<II', machine.mem_read(system + 0x98, 8))
        expected = heuristic(dx, dy)
        if [g, h, parent, nearest, nearest_index, queued] != [121, expected, 1, dx * dx + dy * dy, 0, [0]]:
            failures.append(dict(delta=[dx, dy], actual=[g, h, parent, nearest, nearest_index, queued], heuristic=expected))
            if len(failures) >= 20:
                raise RuntimeError('20 fine heuristic mismatches')
        results[(dx, dy)] = h
        if h > 15 * max(abs(dx), abs(dy)) + 6 * min(abs(dx), abs(dy)):
            overestimates.append([dx, dy, h])
    inconsistent, first = 0, []
    for (dx, dy), h in results.items():
        for sx, sy, cost in ((1, 0, 15), (0, 1, 15), (1, 1, 21), (1, -1, 21)):
            other = results.get((dx + sx, dy + sy))
            if other is not None and abs(h - other) > cost:
                inconsistent += 1
                if len(first) < 10:
                    first.append(dict(a=[dx, dy], b=[dx + sx, dy + sy], h=[h, other], edge_cost=cost))
    report = dict(binary_sha256=digest, function='6f14a560', cases=len(results), mismatches=failures,
                  scope='fresh-node relaxation; goal origin; signed deltas [-256,256]; enqueue stubbed; no complete-search optimality claim',
                  overestimates_of_obstacle_free_15_21_distance=len(overestimates),
                  consistency_violations=inconsistent, examples=first,
                  positive_example=dict(a=[1, 4], b=[2, 5], h=[results[(1, 4)], results[(2, 5)]], edge_cost=21))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(results)} retail heuristic cases; {len(failures)} mismatches; {inconsistent} consistency violations')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
