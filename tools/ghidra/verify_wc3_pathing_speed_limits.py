#!/usr/bin/env python3
"""Execute original normal speed-limit tail and disabled getter gate against C.

Profile/default bound locals and already-computed speed are supplied; complete
public setter/getter/publication witnesses are independently captured live.
Special ability/type overrides and effect-stack production are excluded.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import struct
from pathlib import Path

GAME_SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EIP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    parser.add_argument('--engine-library', type=Path)
    parser.add_argument('--fixture', type=Path)
    args = parser.parse_args()
    binary = args.binary.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != GAME_SHA:
        parser.error('unsupported binary; requires game.dll1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = [struct.unpack_from('<I', binary, opt + o)[0] for o in (28, 56)]
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (size + 4095) & ~4095)
    machine.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        s = opt + struct.unpack_from('<H', binary, pe + 20)[0] + i * 40
        va, count, off = struct.unpack_from('<III', binary, s + 12)
        if count:
            machine.mem_write(base + va, binary[off:off + count])
    machine.mem_map(0x10000000, 0x10000)
    machine.mem_map(0x20000000, 0x10000)
    unit, mover, output, frame, stack, stop = 0x10000000, 0x10001000, 0x10002000, 0x20008000, 0x20009000, 0x30000000
    def write(address, *words):
        machine.mem_write(address, struct.pack('<' + 'I' * len(words), *words))
    def read(address):
        return struct.unpack('<I', machine.mem_read(address, 4))[0]
    def bits(value):
        return struct.unpack('<I', struct.pack('<f', value))[0]
    write(0x6fd3c744, 0)
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_speed_limits.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
    values = [bits(v) for v in (-100, -1, 0, 1, 10, 100, 149, 150, 151, 173, 237, 389, 400, 522, 600, 1000)]
    values += [bits(v) + delta for v in (150, 173, 389, 400, 522) for delta in (-1, 1)]
    rows = []
    for value, profile, defaults, building in itertools.product(values,
            [(0, 0), (173, 389), (-1, 600), (600, -1), (0, 389), (173, 0), (-0.0, -0.0)],
            [(150, 400), (25, 522), (117, 487), (173, 389), (600, 700)], [0, 1]):
        minimum, maximum = [bits(v) for v in profile]
        dmin, dmax = [bits(v) for v in defaults]
        inputs = [value, minimum, maximum, dmin, dmax, 0]
        write(0x6fd709b8, bits(1), bits(522), bits(1), bits(522), dmin, dmax, dmin, dmax)
        write(frame, 0, stop, output)
        write(frame - 0x30, 0, 0, 0)
        write(frame - 0x10, building, unit, maximum, minimum)
        write(output, value)
        machine.reg_write(UC_X86_REG_EBP, frame)
        machine.reg_write(UC_X86_REG_EBX, output)
        machine.reg_write(UC_X86_REG_ESP, frame - 0x30)
        machine.emu_start(0x6f5fc9a4, stop, count=500)
        assert machine.reg_read(UC_X86_REG_EIP) == stop
        outputs = [read(output), read(frame - 4), read(frame - 8)]
        if engine:
            actual = (ctypes.c_uint32 * 3)()
            engine.pathing_speed_limits((ctypes.c_uint32 * 6)(*inputs), actual)
            assert list(actual) == outputs, (inputs, list(actual), outputs)
        rows.append(dict(input=inputs, output=outputs))
    # Full5fc890 early disabled-counter gate, no effect-stack calls or stubs.
    for value in values:
        write(mover + 0x7c, 1)
        write(output, value)
        write(stack, stop, output)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, mover)
        machine.emu_start(0x6f5fc890, stop, count=100)
        assert machine.reg_read(UC_X86_REG_EIP) == stop and read(output) == 0
        inputs, outputs = [value, 0, 0, bits(150), bits(400), 1], [0, 0, 0]
        if engine:
            actual = (ctypes.c_uint32 * 3)()
            engine.pathing_speed_limits((ctypes.c_uint32 * 6)(*inputs), actual)
            assert list(actual) == outputs
        rows.append(dict(input=inputs, output=outputs))
    report = dict(binary_sha256=digest, passed=True, cases=len(rows), disabled_cases=len(values),
                  engine_cases=len(rows) if engine else 0, scope=__doc__)
    if args.fixture:
        args.fixture.write_text(json.dumps(dict(binary_sha256=digest, scope=__doc__, cases=rows), separators=(',', ':')) + '\n')
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
