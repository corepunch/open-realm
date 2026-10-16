#!/usr/bin/env python3
"""Check retail accelerated waypoint acceptance with the downstream consumer stubbed.

Requires Unicorn and the hash-matched local DLL; distributes no retail bytes.
The oracle isolates the original distance/threshold branch, not warp placement.
"""
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
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

    machine.mem_map(0x10000000, 0x10000)
    machine.mem_map(0x20000000, 0x10000)
    path, points, current = 0x10000000, 0x10000200, 0x10000300
    stack, stop = 0x20008000, 0x30000000
    called = []

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    def consume(uc, address, size, data):
        # __thiscall Path_AdvanceAccIndex(enabled, outTeleported), RET 8.
        sp = uc.reg_read(UC_X86_REG_ESP)
        ret, enabled, out = struct.unpack('<III', uc.mem_read(sp, 12))
        called.append(enabled)
        write(out, 0)
        uc.reg_write(UC_X86_REG_EAX, 1)
        uc.reg_write(UC_X86_REG_ESP, sp + 12)
        uc.reg_write(UC_X86_REG_EIP, ret)

    machine.hook_add(UC_HOOK_CODE, consume, begin=0x6f165d10, end=0x6f165d10)
    radius = struct.unpack('<f', machine.mem_read(0x6fcd53f0, 4))[0]
    write(path + 0x60, points)
    machine.mem_write(points + 8, struct.pack('<ff', 81.75, 34.75))
    cases, failures = [], []
    for force in (0, 1):
        for distance in (0, .25, .48, .489, .491, .50, 1, 4):
            for angle in range(0, 360, 30):
                x = 81.75 + distance * math.cos(math.radians(angle))
                y = 34.75 + distance * math.sin(math.radians(angle))
                machine.mem_write(current, struct.pack('<ff', x, y))
                write(path + 0x74, 7, 1)
                write(stack, stop, current, force)
                machine.reg_write(UC_X86_REG_ESP, stack)
                machine.reg_write(UC_X86_REG_ECX, path)
                called.clear()
                machine.emu_start(0x6f165f10, stop, count=10000)
                if machine.reg_read(UC_X86_REG_EIP) != stop:
                    raise RuntimeError('retail waypoint check exceeded instruction budget')
                actual = bool(called)
                expected = bool(force or distance < radius)
                fine_index = struct.unpack('<I', machine.mem_read(path + 0x74, 4))[0]
                row = dict(force=force, distance=distance, angle=angle, accepted=actual,
                           fine_index=fine_index)
                cases.append(row)
                if actual != expected or fine_index != (0xffffffff if actual else 7):
                    failures.append(row)
    report = dict(binary_sha256=digest, function='6f165f10', radius_acc=radius,
                  radius_world=radius * 64, cases=len(cases), mismatches=failures,
                  scope='original acceptance branch; 165d10 stubbed successful, no teleport; excludes exact boundary rounding',
                  samples=cases)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{len(cases)} retail waypoint cases; {len(failures)} mismatches')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
