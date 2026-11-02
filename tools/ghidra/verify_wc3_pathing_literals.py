#!/usr/bin/env python3
"""Original compiled JASS real-token producer, independent model, C parity and live input words."""
import argparse
import ctypes
import hashlib
import json
import random
import re
import struct
from pathlib import Path
from verify_wc3_pathing_numeric import add, divide, integer_float, saturating_integer_word, initialize_runtime_scalars
from generate_wc3_math_tables import reciprocal_table

TARGET = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
RECIPROCALS = reciprocal_table()


def literal_word(text):
    """Integer accumulation modulo32 precedes each software-scalar operation."""
    whole, point, fraction = text.partition('.')
    integer = integer_float(int(whole or '0') & 0xffffffff)
    if not point:
        return integer
    numerator = integer_float(int(fraction or '0') & 0xffffffff)
    denominator = integer_float(pow(10, len(fraction), 1 << 32))
    return add(integer, divide(numerator, denominator, RECIPROCALS))


def source_word(text):
    # The live lexer excludes unary minus from token text; the VM toggles its sign.
    return literal_word(text.lstrip('-')) ^ (0x80000000 if text.startswith('-') else 0)


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path)
    parser.add_argument('--fixture', type=Path, default=Path(__file__).with_name('fixtures') / 'retail-compiled-literal-inputs-1.27.json')
    args = parser.parse_args()
    binary = args.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest() != TARGET:
        parser.error('requires retail game.dll 1.27.1.7085')
    fixture = json.loads(args.fixture.read_text())
    assert fixture['target_sha256'] == TARGET
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
    uc.mem_map(0x10000000, 0x20000)
    uc.mem_map(0x20000000, 0x10000)
    stack, stop, lexer, token = 0x20008000, 0x30000000, 0x10000100, 0x10001000
    startup = initialize_runtime_scalars(uc, stack, stop)
    preserved = [UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP]
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_literal.argtypes = [ctypes.c_char_p]
        engine.pathing_literal.restype = ctypes.c_uint32
    records = []

    def run(text):
        assert re.fullmatch(r'(?:[0-9]+\.[0-9]*|\.[0-9]+)', text), text
        memory = bytearray(b'\xcc' * 0xc8)
        struct.pack_into('<I', memory, 0x98, token)
        text_memory = b'HEAD' + text.encode('ascii') + b'\0TAIL'
        uc.mem_write(lexer, bytes(memory))
        uc.mem_write(token - 4, text_memory)
        uc.mem_write(stack, struct.pack('<I', stop))
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, lexer)
        for i, reg in enumerate(preserved):
            uc.reg_write(reg, 0x12120000 + i)
        uc.emu_start(base + 0x925260, stop, count=100000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop
        assert uc.reg_read(UC_X86_REG_ESP) == stack + 4
        assert uc.reg_read(UC_X86_REG_EAX) == 0x109
        assert [uc.reg_read(r) for r in preserved] == [0x12120000 + i for i in range(4)]
        actual = struct.unpack('<I', uc.mem_read(lexer + 0x24, 4))[0]
        struct.pack_into('<I', memory, 0x24, actual)
        assert bytes(uc.mem_read(lexer, len(memory))) == bytes(memory)
        assert bytes(uc.mem_read(token - 4, len(text_memory))) == text_memory
        assert actual == literal_word(text), (text, hex(actual), hex(literal_word(text)))
        if engine:
            assert engine.pathing_literal(text.encode('ascii')) == actual, ('C', text, hex(actual))
        records.append([text, actual])
        return actual

    boundaries = ['0.59999999999999998', '0.6', '0.0', '4294967297.5', '2147483648.0', '.5', '0.',
                  '0.00000000000000000000000000000001', '1.99999999999999999']
    boundaries += list(dict.fromkeys(c['input'].lstrip('-') for c in fixture['cases']))
    for text in dict.fromkeys(boundaries):
        run(text)
    rng = random.Random(0x127107)
    for _ in range(4000):
        whole = ''.join(str(rng.randrange(10)) for _ in range(rng.randrange(0, 60)))
        fraction = ''.join(str(rng.randrange(10)) for _ in range(rng.randrange(0, 80)))
        run((whole or ('0' if not fraction else '')) + '.' + fraction)
    for case in fixture['cases']:
        word = source_word(case['input'])
        assert word == case['input_word'], ('live input', case['id'], hex(word))
        assert saturating_integer_word(word) == case['output'], ('R2I output', case['id'])
    for row in fixture['literal_sequence']:
        assert row['caller'] == 0x924e74 and row['token'] == 0x109
        assert run(row['text']) == row['output']
    report = dict(binary_sha256=TARGET, helper='6f925260', cases=len(records), live_cases=len(fixture['cases']),
                  live_compilation_events=len(fixture['literal_sequence']),
                  digest=hashlib.sha256(json.dumps(records, separators=(',', ':')).encode()).hexdigest(),
                  runtime_scalar_initializers=startup, engine_exact_cases=len(records) if engine else 0,
                  engine_library_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest() if engine else None,
                  contracts=['ECX lexer, RET plain, EAX token109, only lexer+24 is written',
                             'Unsigned decimal token; each integer/fraction/divisor accumulator wraps32 before scalar conversion',
                             'Unlimited fractional digits; FromInteger(numerator)/FromInteger(divisor) + FromInteger(whole)',
                             'Live compilation verifies ordinary/.5/0./unary signs, prefix/fraction/divisor overflow, and raw public R2I inputs'],
                  violations=[])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
