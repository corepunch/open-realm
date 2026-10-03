#!/usr/bin/env python3
"""Execute the original sibling CRT byte classifier and the composed retail decimal parser."""
import argparse
import ctypes
import hashlib
import json
import struct
from pathlib import Path
from generate_wc3_math_tables import reciprocal_table
from verify_wc3_pathing_numeric import decimal_bits, initialize_runtime_scalars
from wc3_shipped_crt import load_crt

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
    opt = pe+24
    base, size = [struct.unpack_from('<I', binary, opt+n)[0] for n in (28, 56)]
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (size+4095) & ~4095)
    uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt+60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe+6)[0]):
        section = opt+struct.unpack_from('<H', binary, pe+20)[0]+40*i
        va, count, offset = struct.unpack_from('<III', binary, section+12)
        if count:
            uc.mem_write(base+va, binary[offset:offset+count])
    uc.mem_map(0x10000000, 0x10000)
    uc.mem_map(0x20000000, 0x10000)
    stack, stop, output, text = 0x20008000, 0x30000000, 0x10000100, 0x10001000
    preserved = [UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP]

    def write(address, *values):
        uc.mem_write(address, struct.pack('<'+'I'*len(values), *(v & 0xffffffff for v in values)))

    def read(address):
        return struct.unpack('<I', uc.mem_read(address, 4))[0]

    startup = initialize_runtime_scalars(uc, stack, stop)
    crt = load_crt(uc, args.binary.parent/'msvcr120.dll')
    cb = crt['base']
    digit = crt['exports']['isdigit']
    assert digit == cb+0xf1d5 and read(cb+0xdf7c4) == 0 and read(cb+0xdf858) == cb+0x1158
    default_locale = read(cb+0xdfa84)
    assert read(default_locale+0x74) == 1 and read(default_locale+0x90) == cb+0x1158
    write(0x6fa7c4fc, digit)
    # Explicit original C-locale context avoids supplying the CRT's TLS machinery.
    explicit_locale = 0x10000400
    write(explicit_locale, default_locale, read(cb+0xdfca8))
    ctype_table = bytes(uc.mem_read(cb+0x1058, 384*2))
    classified = []
    for name in ('isdigit', '_isdigit_l', '_isctype', '_isctype_l'):
        # _isctype_l treats values below EOF (-1) as multibyte characters and
        # calls Windows GetStringType; this is outside the S2R/default path.
        for value in range(-1 if name == '_isctype_l' else -128, 256):
            arguments = [value]
            if name in ('_isctype', '_isctype_l'):
                arguments.append(4)
            if name in ('_isdigit_l', '_isctype_l'):
                arguments.append(explicit_locale)
            write(stack, stop, *arguments)
            uc.reg_write(UC_X86_REG_ESP, stack)
            for i, reg in enumerate(preserved):
                uc.reg_write(reg, 0x12120000+i)
            try:
                uc.emu_start(crt['exports'][name], stop, count=10000)
            except Exception as error:
                raise RuntimeError(f'{name}({value}) at EIP={uc.reg_read(UC_X86_REG_EIP):#x}') from error
            assert uc.reg_read(UC_X86_REG_EIP) == stop and uc.reg_read(UC_X86_REG_ESP) == stack+4
            assert [uc.reg_read(reg) for reg in preserved] == [0x12120000+i for i in range(4)]
            actual = uc.reg_read(UC_X86_REG_EAX)
            assert actual == (4 if 48 <= value <= 57 else 0), (name, value, actual)
            classified.append([name, value, actual])
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_decimal.argtypes = [ctypes.c_char_p]
        engine.pathing_decimal.restype = ctypes.c_uint32
    observed = []

    def observe(machine, address, instruction_size, user_data):
        word = read(machine.reg_read(UC_X86_REG_ESP)+4)
        signed = word-(1 << 32) if word & 0x80000000 else word
        assert -128 <= signed <= 127, signed
        observed.append(signed)

    uc.hook_add(UC_HOOK_CODE, observe, begin=digit, end=digit)
    uc.ctl_flush_tb()
    records = []
    recips = reciprocal_table()
    for value in range(1, 256):
        for source in (bytes([value]), b'12'+bytes([value])+b'34', b'.5'+bytes([value])+b'7', b'-'+bytes([value])+b'0.2'):
            observed.clear()
            guarded = b'HEAD'+source+b'\0TAIL'
            uc.mem_write(text-4, guarded)
            write(output-4, 0xabcdef01, 0xdeadbeef, 0xabcdef02)
            write(stack, stop)
            uc.reg_write(UC_X86_REG_ESP, stack)
            uc.reg_write(UC_X86_REG_ECX, output)
            uc.reg_write(UC_X86_REG_EDX, text)
            for i, reg in enumerate(preserved):
                uc.reg_write(reg, 0x12120000+i)
            uc.emu_start(base+0x070de0, stop, count=100000)
            assert uc.reg_read(UC_X86_REG_EIP) == stop and uc.reg_read(UC_X86_REG_ESP) == stack+4
            assert uc.reg_read(UC_X86_REG_EAX) == output
            assert [uc.reg_read(reg) for reg in preserved] == [0x12120000+i for i in range(4)]
            assert read(output-4) == 0xabcdef01 and read(output+4) == 0xabcdef02
            assert bytes(uc.mem_read(text-4, len(guarded))) == guarded
            actual = read(output)
            assert actual == decimal_bits(source.decode('latin1'), recips), (source.hex(), hex(actual))
            if engine:
                assert engine.pathing_decimal(source) == actual, ('C-byte-parser', source.hex(), hex(actual))
            if value >= 128:
                assert value-256 in observed, (source.hex(), observed)
            records.append([source.hex(), actual, list(observed)])
    assert read(cb+0xdf7c4) == 0 and read(cb+0xdf858) == cb+0x1158
    digest = lambda rows: hashlib.sha256(json.dumps(rows, separators=(',', ':')).encode()).hexdigest()
    report = dict(status='decimal-ctype-exact', binary_sha256=TARGET, target_sha256=TARGET, crt_sha256=crt['sha256'],
                  classifier_cases=len(classified), classifier_sha256=digest(classified),
                  parser_cases=len(records), parser_sha256=digest(records),
                  engine_exact_cases=len(records) if engine else 0,
                  ctype_table_sha256=hashlib.sha256(ctype_table).hexdigest(),
                  locale_ever_changed=0, ctype_rva=0x1158, default_mb_cur_max=1,
                  exports={name:hex(crt['exports'][name]-cb) for name in ('isdigit','_isdigit_l','_isctype','_isctype_l')},
                  shared_scalar_startup=startup,
                  scope='Original code, default C-locale and explicit default locale; signed byte promotion, guards, stack, nonvolatile registers and independent scalar model checked',
                  exclusions=['CRT DLL startup/TLS and alternative locale creation', '_isctype_l multibyte values outside -1..255 need Windows character services', 'Live public byte production is separately captured', 'A NUL terminates input and cannot be an interior parser byte'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
