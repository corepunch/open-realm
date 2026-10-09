#!/usr/bin/env python3
"""Original decimal/octal/hex source integer actions, independent wrapping model and exact C/live words."""
import argparse
import ctypes
import hashlib
import json
import random
import re
import struct
from pathlib import Path
from verify_wc3_pathing_numeric import integer_float

TARGET = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def token_profile(text):
    if text.startswith('$'):
        return 16, 1, 0x925350
    if text.startswith(('0x', '0X')):
        return 16, 2, 0x925350
    if text.startswith('0'):
        return 8, 1, 0x925490
    return 10, 0, 0x925210


def integer_literal_word(text):
    radix, prefix, _ = token_profile(text)
    return int(text[prefix:] or '0', radix) & 0xffffffff


def source_word(text):
    result = integer_literal_word(text.lstrip('-'))
    return (-result if text.startswith('-') else result) & 0xffffffff


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path)
    parser.add_argument('--fixture', type=Path, default=Path(__file__).with_name('fixtures') / 'retail-compiled-integer-inputs-1.27.json')
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
    preserved = [UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP]
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_integer_literal.argtypes = [ctypes.c_char_p]
        engine.pathing_integer_literal.restype = ctypes.c_uint32
    records = []
    counts = dict(decimal=0, octal=0, dollar_hex=0, prefixed_hex=0)

    def run(text):
        radix, prefix, rva = token_profile(text)
        digits = text[prefix:]
        assert (not digits and text == '0') or re.fullmatch({8:r'[0-7]+', 10:r'[0-9]+', 16:r'[0-9a-fA-F]+'}[radix], digits), text
        memory = bytearray(b'\xcc' * 0xc8)
        struct.pack_into('<I', memory, 0x98, token)
        struct.pack_into('<I', memory, 0xc4, len(text))
        text_memory = b'HEAD' + text.encode('ascii') + b'\0TAIL'
        uc.mem_write(lexer, bytes(memory))
        uc.mem_write(token - 4, text_memory)
        uc.mem_write(stack, struct.pack('<II', stop, prefix))
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, lexer)
        for i, reg in enumerate(preserved):
            uc.reg_write(reg, 0x12120000 + i)
        uc.emu_start(base + rva, stop, count=100000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop
        assert uc.reg_read(UC_X86_REG_ESP) == stack + (8 if radix == 16 else 4)
        assert uc.reg_read(UC_X86_REG_EAX) == 0x108
        assert [uc.reg_read(r) for r in preserved] == [0x12120000 + i for i in range(4)]
        actual = struct.unpack('<I', uc.mem_read(lexer + 0x24, 4))[0]
        struct.pack_into('<I', memory, 0x24, actual)
        assert bytes(uc.mem_read(lexer, len(memory))) == bytes(memory)
        assert bytes(uc.mem_read(token - 4, len(text_memory))) == text_memory
        assert actual == integer_literal_word(text), (text, hex(actual))
        if engine:
            assert engine.pathing_integer_literal(text.encode('ascii')) == actual, ('C', text, hex(actual))
        counts[{8:'octal', 10:'decimal', 16:'dollar_hex' if prefix == 1 else 'prefixed_hex'}[radix]] += 1
        records.append([text, actual])
        return actual

    for text in dict.fromkeys(c['input'].lstrip('-') for c in fixture['cases']):
        run(text)
    rng = random.Random(0x925210)
    for radix, prefix in [(10, ''), (8, '0'), (16, '$'), (16, '0x')]:
        alphabet = '0123456789abcdef'[:radix]
        for _ in range(4000):
            digits = ''.join(rng.choice(alphabet) for _ in range(rng.randrange(1, 81)))
            if radix == 10:
                digits = rng.choice('123456789') + digits
            if radix == 16 and rng.randrange(2):
                digits = digits.upper()
            run(prefix + digits)
    for case in fixture['cases']:
        word = source_word(case['input'])
        assert word == case['input_word'], ('live input', case['id'], hex(word))
        assert integer_float(word) == case['output'], ('I2R output', case['id'])
    for row in fixture['integer_sequence']:
        radix, prefix, rva = token_profile(row['text'])
        expected_caller = {0x925210:0x924e38, 0x925490:0x924e46}.get(rva, 0x924e56 if prefix == 1 else 0x924e66)
        assert row['caller'] == expected_caller and row['token'] == 0x108
        assert row['radix'] == radix and row['prefix'] == prefix
        assert run(row['text']) == row['output']
    report = dict(binary_sha256=TARGET, helpers=['6f925210', '6f925490', '6f925350'], cases=len(records),
                  helper_cases=counts, live_cases=len(fixture['cases']), live_compilation_events=len(fixture['integer_sequence']),
                  digest=hashlib.sha256(json.dumps(records, separators=(',', ':')).encode()).hexdigest(),
                  engine_exact_cases=len(records) if engine else 0,
                  engine_library_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest() if engine else None,
                  contracts=['ECX lexer; token text+98 and lengthc4, only result+24 is written; EAX token108',
                             'Decimal/octal RET plain; hex stack4 prefix length, RET4; nonvolatile/stack/text/lexer guards',
                             'Modulo32 accumulation per digit, separate modulo32 unary minus including INT_MIN',
                             'Live decimal/octal/$hex/0xhex source constants and actual I2R raw inputs/outputs'], violations=[])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
