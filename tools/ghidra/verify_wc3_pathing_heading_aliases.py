#!/usr/bin/env python3
"""Observe original vector-heading operand relationships and compare the engine's exact words.

Supplied scalar/vector backing exercises 1d4c80 and the complete 16f630 producer.
It does not establish all movement callers' input domains or whole-frame timing.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import struct
from collections import Counter
from pathlib import Path
from generate_wc3_math_tables import acos_tables, reciprocal_table
from verify_wc3_pathing_numeric import (acos_bits, add, bits, divide, initialize_runtime_scalars,
                                      multiply, square_root, subtract)

TARGET = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX,
                                  UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path)
    args = parser.parse_args()
    binary = args.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest() != TARGET:
        parser.error('requires retail game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = [struct.unpack_from('<I', binary, opt + n)[0] for n in (28, 56)]
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (size + 4095) & ~4095)
    uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + i * 40
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            uc.mem_write(base + va, binary[offset:offset + count])
    uc.mem_map(0x10000000, 0x10000)
    uc.mem_map(0x20000000, 0x10000)
    stack, stop = 0x20008000, 0x30000000
    vector, length, current, output = 0x10000100, 0x10000200, 0x10000300, 0x10000400
    preserved = [UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP]

    def write(address, *words):
        uc.mem_write(address, struct.pack('<' + 'I' * len(words), *(w & 0xffffffff for w in words)))

    def read(address):
        return struct.unpack('<I', uc.mem_read(address, 4))[0]

    startup = initialize_runtime_scalars(uc, stack, stop)
    ordinary, near = acos_tables()
    assert list(struct.unpack('<1020I', uc.mem_read(0x6fa830d0, 4080))) == ordinary
    assert list(struct.unpack('<138I', uc.mem_read(0x6fa840d8, 552))) == near
    recips = reciprocal_table()
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_heading_error.argtypes = [ctypes.c_uint32] * 3
        engine.pathing_heading_error.restype = ctypes.c_uint32
        engine.pathing_vector_heading.argtypes = [ctypes.c_uint32] * 2
        engine.pathing_vector_heading.restype = ctypes.c_uint32

    observed, examples = Counter(), {}
    active = None

    def observe(machine, address, size, user_data):
        if active is None:
            return
        sp = machine.reg_read(UC_X86_REG_ESP)
        dst, src = machine.reg_read(UC_X86_REG_ECX), machine.reg_read(UC_X86_REG_EDX)
        caller = read(sp)
        key = (active, f'{caller:08x}', 'alias' if dst == src else 'distinct')
        observed[key] += 1
        examples.setdefault(key, dict(output=f'{dst:08x}', input=f'{src:08x}', input_word=read(src)))
        if active in ('vector', 'heading'):
            assert caller == 0x6f1d4cbf and dst != src, ('unexpected Acos producer', key, dst, src)
            assert dst == sp + 24 and src == sp + 8, ('changed vector-heading local slots', dst, src, sp)

    uc.hook_add(UC_HOOK_CODE, observe, begin=0x6f06ffa0, end=0x6f06ffa0)
    uc.ctl_flush_tb()

    def run(entry, registers, argv):
        ecx, edx = registers
        write(stack, stop, *argv)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        for i, reg in enumerate(preserved):
            uc.reg_write(reg, 0x12120000 + i)
        uc.emu_start(entry, stop, count=10000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop
        assert uc.reg_read(UC_X86_REG_ESP) == stack + 4 * (1 + len(argv))
        assert [uc.reg_read(reg) for reg in preserved] == [0x12120000 + i for i in range(4)]
        assert uc.reg_read(UC_X86_REG_EAX) == (argv[0] if entry == 0x6f1d4c80 else ecx)

    def setup(x, y, h, n):
        write(vector - 4, 0xabcdef01, x, y, 0xabcdef02)
        for addr, value in ((length, n), (current, h), (output, 0xdeadbeef)):
            write(addr - 4, 0xabcdef03, value, 0xabcdef04)

    def guards(dst, before):
        for addr, expected in before.items():
            if addr != dst:
                assert read(addr) == expected, ('unexpected write', hex(addr), hex(read(addr)), hex(expected))

    rows, negatives = [], []
    # The direct helper's alias behavior is a negative control, not a movement result.
    for word in (bits(-1), bits(-0.999), bits(-0.5), bits(-0.125), bits(0.5)):
        for aliased in (False, True):
            setup(word, 0, 0, 0)
            dst = vector if aliased else output
            active = 'direct-acos'
            run(0x6f06ffa0, (dst, vector), [])
            expected = acos_bits(word & 0x7fffffff if aliased else word, ordinary, near)
            assert read(dst) == expected
            negatives.append([word, aliased, read(dst)])
    assert any(a[2] != b[2] for a, b in zip(negatives[::2], negatives[1::2]))
    values = (-32, -4, -1, -0.125, -2**-16, -2**-24, -0.0, 0, 2**-24, 2**-16, 0.125, 1, 4, 32)
    headings = (0, 0.125, 1, 3, 5, 6)
    for x, y in itertools.product(values, repeat=2):
        wx, wy = bits(x), bits(y)
        n = square_root(add(multiply(wx, wx), multiply(wy, wy)))
        angle = 0 if n & 0x7fffffff <= 0x3727c5ac else acos_bits(divide(wx, n, recips), ordinary, near)
        if angle & 0x7fffffff <= 0x3727c5ac:
            angle = 0
        elif y < 0:
            angle = subtract(0x40c90fdb, angle)
        if engine:
            assert engine.pathing_vector_heading(wx, wy) == angle
        for dst in (output, vector, vector + 4, length):
            setup(wx, wy, 0, n)
            before = {a: read(a) for a in (vector - 4, vector, vector + 4, vector + 8,
                      length - 4, length, length + 4, current - 4, current, current + 4,
                      output - 4, output, output + 4)}
            active = 'vector'
            run(0x6f1d4c80, (vector, 0), [dst, length])
            assert read(dst) == angle
            guards(dst, before)
            rows.append(['vector', wx, wy, dst - vector, angle])
        for h in headings:
            wh = bits(h)
            reverse = subtract(wh, angle)
            val = struct.unpack('<f', struct.pack('<I', reverse))[0]
            pi = struct.unpack('<f', struct.pack('<I', 0x40490fdb))[0]
            if val < -pi:
                error = subtract(0xc0c90fdb, reverse)
            elif val > pi:
                error = subtract(0x40c90fdb, reverse)
            else:
                error = reverse ^ 0x80000000
            if subtract(error, 0) & 0x7fffffff < 0x3456bf95:
                error = 0
            if engine:
                assert engine.pathing_heading_error(wx, wy, wh) == error
            for dst in (output, vector, vector + 4, current):
                setup(wx, wy, wh, n)
                before = {a: read(a) for a in (vector - 4, vector, vector + 4, vector + 8,
                          length - 4, length, length + 4, current - 4, current, current + 4,
                          output - 4, output, output + 4)}
                active = 'heading'
                run(0x6f16f630, (dst, current), [vector])
                assert read(dst) == error, (wx, wy, wh, dst, read(dst), error)
                guards(dst, before)
                rows.append(['heading', wx, wy, wh, dst - vector, error])
    report = dict(passed=True, binary_sha256=TARGET, startup=startup, scope=__doc__,
                  vector_cases=sum(row[0] == 'vector' for row in rows),
                  heading_cases=sum(row[0] == 'heading' for row in rows),
                  negative_controls=negatives, engine_checked=bool(engine),
                  word_sequence_sha256=hashlib.sha256(json.dumps(rows, separators=(',', ':')).encode()).hexdigest(),
                  observations=[dict(producer=k[0], caller=k[1], relation=k[2], count=v, example=examples[k])
                                for k, v in sorted(observed.items())])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
