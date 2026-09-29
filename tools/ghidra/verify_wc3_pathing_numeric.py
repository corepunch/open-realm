#!/usr/bin/env python3
"""Bit-exact retail 1.27 arithmetic and composed spatial-bound oracle.

Reference models operate on integers, independently of the emulated helpers.
Raw-bit probes establish helper behavior, not validity at gameplay producers.
"""
import argparse
import hashlib
import itertools
import json
import math
import random
import struct
import ctypes
from pathlib import Path

MASK = 0xffffffff
SIGN = 0x80000000
FRAC = 0x7fffff


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def add(a, b):
    ea, eb = (a >> 23) & 255, (b >> 23) & 255
    if not ea:
        return b
    if not eb:
        return a
    if eb - ea >= 23:
        return b
    if ea - eb >= 23:
        return a
    exponent = max(ea, eb)
    terms = [((w & FRAC) | 0x800000) * 2 * (-1 if w & SIGN else 1)
             for w in (a, b)]
    total = (terms[0] >> (exponent-ea)) + (terms[1] >> (exponent-eb))
    if not total:
        return 0
    magnitude = abs(total)
    shift = magnitude.bit_length() - 24
    mantissa = magnitude >> shift if shift >= 0 else magnitude << -shift
    return (((exponent+shift-1) << 23) | (mantissa & FRAC) |
            (SIGN if total < 0 else 0)) & MASK


def subtract(a, b):
    return add(a, b ^ SIGN)


def multiply(a, b):
    ea, eb = (a >> 23) & 255, (b >> 23) & 255
    ma, mb = a & FRAC, b & FRAC
    exponent = ea + eb - 127
    if not ma or not mb:
        if not ea or not eb or not 1 <= exponent <= 256:
            return 0
        return ((exponent << 23) | ma | mb | ((a ^ b) & SIGN)) & MASK
    # The signed exponent-word guard accepts pre-normalization exponents
    # 1..256. It flushes tiny products even when normalization would restore
    # a normal result, and flushes extreme overflow beyond this interval.
    if not 1 <= exponent <= 256:
        return 0
    product = (ma | 0x800000) * (mb | 0x800000)
    shift = product.bit_length() - 24
    return (((exponent + shift - 23) << 23) | ((product >> shift) & FRAC) |
            ((a ^ b) & SIGN)) & MASK


def square_root(word):
    if word & SIGN or not word:
        return 0
    fraction = word & FRAC
    radicand = ((fraction | 0x800000) << 8) | (fraction >> 15)
    root = math.isqrt(radicand)
    coefficient = ((((root - 0xb504) * 0xb505) & MASK) >> 8) | 0x3f800000
    exponent = ((word >> 23) & 255) - 127
    # Signed division in the original rounds (exponent - parity) toward zero.
    adjusted = exponent - (exponent & 1)
    half = adjusted // 2
    scale = ((half+127) << 23) | (0x3504f3 if exponent & 1 else 0)
    return multiply(scale, coefficient)


def reciprocal(word, table):
    fraction = word & FRAC
    index, residue = fraction >> 13, fraction & 8191
    # Thirteen-bit residual repeated across the 32-bit interpolation fraction.
    weight = ((residue << 19) | (residue << 6) | (residue >> 7)) & MASK
    interpolation = table[index] - ((table[index]-table[index+1])*weight >> 32)
    result = (interpolation - (word & 0x7f800000) + 0x7e800000) & MASK
    if ((result - 0x800000) & MASK) & SIGN:
        return 0
    return result | (word & SIGN)


def divide(a, b, table):
    return bits(1) if a == b else multiply(a, reciprocal(b, table))


def floor_word(word):
    exponent = ((word >> 23) & 255) - 127
    if exponent < 0:
        return bits(-1) if word & SIGN and word & ~SIGN else 0
    if exponent >= 23:
        return word
    fraction_mask = (1 << (23-exponent)) - 1
    rounded = word & ~fraction_mask
    if word & SIGN and word & fraction_mask:
        rounded += fraction_mask + 1
    return rounded & MASK


def integer_word(word):
    exponent = (word >> 23) & 255
    if exponent < 127:
        return 0
    mantissa = (word & FRAC) | 0x800000
    shift = exponent - 150
    magnitude = (mantissa << (shift & 31)) if shift >= 0 else (mantissa >> ((-shift) & 31))
    return (-magnitude if word & SIGN else magnitude) & MASK


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX,
        UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI,
        UC_X86_REG_EDI, UC_X86_REG_EBP)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    parser.add_argument('--engine-library', type=Path, help='compiled wc3_pathing_engine_probe.c; compare exact C output bits')
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        for name in ('add', 'subtract', 'multiply'):
            proc = getattr(engine, 'pathing_' + name)
            proc.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
            proc.restype = ctypes.c_uint32
    binary = args.binary.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        parser.error('requires retail game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = [struct.unpack_from('<I', binary, opt+n)[0] for n in (28, 56)]
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (size+4095) & ~4095)
    uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt+60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe+6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe+20)[0] + i*40
        va, count, offset = struct.unpack_from('<III', binary, section+12)
        if count:
            uc.mem_write(base+va, binary[offset:offset+count])
    uc.mem_map(0x10000000, 0x10000)
    uc.mem_map(0x20000000, 0x10000)
    stack, stop = 0x20008000, 0x30000000
    left, right, output = 0x10000100, 0x10000200, 0x10000300
    preserved = [UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP]
    def write(address, *words):
        uc.mem_write(address, struct.pack('<'+'I'*len(words), *(w & MASK for w in words)))
    def read(address):
        return struct.unpack('<I', uc.mem_read(address, 4))[0]
    write(0x6fd3c740, bits(-1), 0, bits(1))
    def call(entry, a, b=None, alias=0):
        write(left-4, 0xabcdef01, a, 0xabcdef02)
        write(right-4, 0xabcdef03, b or 0, 0xabcdef04)
        write(output-4, 0xabcdef05, 0xdeadbeef, 0xabcdef06)
        destination = [output, left, right][alias]
        write(stack, stop, right)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, left if entry == 0x6f070120 else destination)
        uc.reg_write(UC_X86_REG_EDX, left)
        for i, reg in enumerate(preserved):
            uc.reg_write(reg, 0x12120000+i)
        uc.emu_start(entry, stop, count=10000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop
        assert uc.reg_read(UC_X86_REG_ESP) == stack + (8 if b is not None else 4)
        assert [uc.reg_read(reg) for reg in preserved] == [0x12120000+i for i in range(4)]
        for address, sentinel in [(left-4,0xabcdef01),(left+4,0xabcdef02),
                (right-4,0xabcdef03),(right+4,0xabcdef04),(output-4,0xabcdef05),(output+4,0xabcdef06)]:
            assert read(address) == sentinel
        if entry == 0x6f070120:
            assert read(left) == a
            return uc.reg_read(UC_X86_REG_EAX)
        assert uc.reg_read(UC_X86_REG_EAX) == destination
        if destination != left:
            assert read(left) == a
        if destination != right:
            assert read(right) == (b or 0)
        return read(destination)
    rng = random.Random(0x12717085)
    words = set([0, SIGN, 1, SIGN|1, FRAC, SIGN|FRAC])
    for exponent, mantissa, sign in itertools.product(
            [1,2,64,103,104,105,126,127,128,149,150,151,157,158,181,253,254,255],
            [0,1,0x3fffff,0x400000,FRAC-1,FRAC], [0,SIGN]):
        words.add(sign | (exponent << 23) | mantissa)
    counts = {}
    for name, entry, model in [('add',0x6f06fbb0,add),('subtract',0x6f06fa90,subtract),
                                ('multiply',0x6f06f9c0,multiply)]:
        pairs = [(a,b) for a in sorted(words) for b in [0,SIGN,a,a^SIGN,bits(1),bits(-1)]]
        pairs += [(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(20000)]
        for exponent, gap, sign in itertools.product([30,100,127,150,200],[21,22,23,24],[0,SIGN]):
            pairs.append(((exponent << 23)|1, sign|((exponent-gap)<<23)|FRAC))
        for a,b in pairs:
            actual = call(entry,a,b)
            if engine:
                assert getattr(engine, 'pathing_' + name)(a, b) == actual, (name, hex(a), hex(b), hex(actual))
            assert actual == model(a,b), (name,hex(a),hex(b),hex(actual),hex(model(a,b)))
        for a,b in pairs[:200]:
            for alias in [1,2]:
                assert call(entry,a,b,alias) == model(a,b)
        counts[name] = len(pairs)+400
    unary = sorted(words) + [rng.getrandbits(32) for _ in range(20000)]
    for name, entry, model in [('floor',0x6f070c80,floor_word),('integer',0x6f070120,integer_word)]:
        for word in unary:
            actual = call(entry,word)
            assert actual == model(word), (name,hex(word),hex(actual),hex(model(word)))
        if name == 'floor':
            for word in unary[:200]:
                assert call(entry,word,alias=1) == model(word)
        counts[name] = len(unary) + (200 if name == 'floor' else 0)
    root_rng = random.Random(0x71530)
    integer_roots = [0,1,MASK] + [root_rng.getrandbits(32) for _ in range(20000)]
    integer_roots += [root*root+offset for root in range(1,65536,127) for offset in [-1,0,1]]
    for radicand in integer_roots:
        write(stack,stop)
        uc.reg_write(UC_X86_REG_ESP,stack)
        uc.reg_write(UC_X86_REG_ECX,radicand)
        for i,reg in enumerate(preserved):
            uc.reg_write(reg,0x12120000+i)
        uc.emu_start(0x6f071530,stop,count=10000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop
        assert uc.reg_read(UC_X86_REG_ESP) == stack+4
        assert uc.reg_read(UC_X86_REG_EAX) == math.isqrt(radicand)
        assert [uc.reg_read(reg) for reg in preserved] == [0x12120000+i for i in range(4)]
    counts['integer_sqrt'] = len(integer_roots)
    # Embedded table values are authoritative inputs, not independently
    # regenerated constants. Arithmetic/interpolation is modeled separately.
    reciprocal_table = list(struct.unpack('<1025I',uc.mem_read(0x6fa810c0,4100)))
    table_hash = hashlib.sha256(uc.mem_read(0x6fa810c0,4100)).hexdigest()
    extended_unary = unary + [((127 << 23) | (i << 13) | residual)
                             for i in range(1024) for residual in [0,1,4095,8190,8191]]
    for name,entry,model in [('sqrt',0x6f071480,square_root),
                            ('reciprocal',0x6f0711e0,lambda a: reciprocal(a,reciprocal_table))]:
        for word in extended_unary:
            actual = call(entry,word)
            assert actual == model(word),(name,hex(word),hex(actual),hex(model(word)))
        for word in extended_unary[:200]:
            assert call(entry,word,alias=1) == model(word)
        counts[name] = len(extended_unary)+200
    division_pairs = [(a,b) for a in sorted(words) for b in [0,SIGN,a,a^SIGN,bits(1),bits(-1)]]
    division_pairs += [(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(20000)]
    for a,b in division_pairs:
        actual = call(0x6f06fcd0,a,b)
        assert actual == divide(a,b,reciprocal_table),('divide',hex(a),hex(b),hex(actual))
    for a,b in division_pairs[:200]:
        for alias in [1,2]:
            assert call(0x6f06fcd0,a,b,alias) == divide(a,b,reciprocal_table)
    counts['divide'] = len(division_pairs)+400
    # Complete Path normalizer: returns length; modifies XY only for length>1.
    vector = 0x10000800
    vectors = [(bits(x),bits(y)) for x,y in itertools.product(
        [-4096,-16,-1,-.499,-.125,0,.125,.499,1,16,4096],repeat=2)]
    vectors += [(bits(rng.uniform(-100,100)),bits(rng.uniform(-100,100))) for _ in range(2000)]
    vectors += [(bits(1)+delta,bits(y)) for delta,y in itertools.product([-1,0,1],[0,.0001,.5])]
    for x,y in vectors:
        write(vector-4,0xabcdef01,x,y,0xabcdef02)
        write(output-4,0xabcdef03,0xdeadbeef,0xabcdef04)
        write(stack,stop)
        uc.reg_write(UC_X86_REG_ESP,stack)
        uc.reg_write(UC_X86_REG_ECX,output)
        uc.reg_write(UC_X86_REG_EDX,vector)
        for i,reg in enumerate(preserved):
            uc.reg_write(reg,0x12120000+i)
        uc.emu_start(0x6f168280,stop,count=10000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop
        assert uc.reg_read(UC_X86_REG_ESP) == stack+4
        assert uc.reg_read(UC_X86_REG_EAX) == output
        assert [uc.reg_read(reg) for reg in preserved] == [0x12120000+i for i in range(4)]
        length = square_root(add(multiply(x,x),multiply(y,y)))
        wanted = (x,y) if length <= bits(1) else tuple(
            multiply(v,reciprocal(length,reciprocal_table)) for v in [x,y])
        assert read(output) == length,(hex(x),hex(y),hex(read(output)),hex(length))
        assert (read(vector),read(vector+4)) == wanted
        if x == bits(1)+1 and y == 0:
            assert length == bits(1) and wanted == (x,y)
        assert [read(a) for a in [vector-4,vector+8,output-4,output+4]] == [
            0xabcdef01,0xabcdef02,0xabcdef03,0xabcdef04]
    normalizer_cases = len(vectors)
    # Compose the real bound constructor through its original scalar helpers;
    # stop at spatial mutation entry, never patch/stub its instructions.
    mover, obj, grid, point, radius = [0x10001000+n for n in (0,0x200,0x400,0x600,0x700)]
    write(mover+0x94,obj)
    write(obj+0x2c,grid)
    positions = []
    for value in [0.125,0.5,1,8,8.999,16,127.5,4096]:
        positions.extend([bits(value)-1,bits(value),bits(value)+1])
    positions += [w ^ SIGN for w in positions]
    bound_count = 0
    witness = None
    for x,r,s in itertools.product(positions,[bits(v) for v in [0,.125,.499,.5,1,1.5]],
                                      [bits(v) for v in [.125,1,2]]):
        write(point,x,x^SIGN)
        write(radius,r)
        write(grid+0x68,s)
        write(stack,stop,radius,point)
        uc.reg_write(UC_X86_REG_ESP,stack)
        uc.reg_write(UC_X86_REG_ECX,mover)
        uc.emu_start(0x6f1604d0,0x6f14e770,count=10000)
        assert uc.reg_read(UC_X86_REG_EIP) == 0x6f14e770
        rectangle = read(uc.reg_read(UC_X86_REG_ESP)+4)
        actual = struct.unpack('<4I',uc.mem_read(rectangle,16))
        wanted = tuple((integer_word(floor_word(multiply(fn(v,r),s)))+upper)&MASK
                       for v,fn,upper in [(x^SIGN,subtract,0),(x,subtract,0),
                                          (x^SIGN,add,1),(x,add,1)])
        assert actual == wanted,(hex(x),hex(r),hex(s),actual,wanted)
        assert uc.reg_read(UC_X86_REG_ECX) == obj
        if x == bits(8.999) and r == bits(.499) and s == bits(2):
            assert actual[1] == 16
            def host_value(word):
                return struct.unpack('<f',struct.pack('<I',word))[0]
            nearest = host_value(bits(host_value(x)-host_value(r)))
            assert math.floor(host_value(bits(nearest*host_value(s)))) == 17
            witness = dict(x_bits=hex(x),radius_bits=hex(r),scale_bits=hex(s),retail_min_x=16,
                           ieee_nearest_min_x=17)
        bound_count += 1
    assert witness
    report = dict(passed=True,binary_sha256=digest,helper_cases=counts,
        composed_normalizer_cases=normalizer_cases,
        normalization_threshold_witness={'input_x':'3f800001','input_y':'00000000',
            'retail_length':'3f800000','output_x':'3f800001','output_y':'00000000'},
        reciprocal_table={"address":"6fa810c0","words":1025,"sha256":table_hash},
        composed_bound_prefix_cases=bound_count,occupied_cell_witness=witness,
        reference='Independent integer-bit models; exact outputs, no tolerance; unmodified retail code',
        random_seed='0x12717085', random_pair_cases_per_binary_helper=20000,
        abi_checks=['Return pointer/integer, balanced callee-cleaned stack, callee-saved registers',
                    'Input preservation, output guard words, binary output aliases either input',
                    'Unary floor/sqrt/reciprocal output aliases input'],
        initialized_constants={'6fd3c740':'-1', '6fd3c744':'0', '6fd3c748':'1'},
        contracts={'divide':'Raw-word equality returns 1, otherwise reciprocal-table interpolation followed by retail multiply',
                   'sqrt':'Negative and signed-zero inputs return +0; 16-bit integer square root of replicated significand, fixed coefficients b504/b505 and parity scale3504f3, retail multiply',
                   'reciprocal':'1025-word embedded table; index mantissa bits13..22; repeated 13-bit residual interpolation; exponent correction and signed-word range guard',
                   'add_sub':'Exponent-zero operand shortcut; exponent gap >=23 discards smaller operand; signed doubled-significand arithmetic alignment; truncating normalization; 32-bit exponent wrap',
                   'multiply':'Truncated 24-bit product; pre-normalization exponent guard 1..256; zero-fraction shortcut handles exponent-zero operands separately',
                   'floor':'Negative nonzero values below one become -1; both signed zeros become +0; fractional mantissa truncation with negative ceiling of magnitude; exponent >=150 unchanged',
                   'integer':'Truncation toward zero for ordinary values; exponent <127 returns zero; larger exponents use x86 modulo-32 shifts and modulo-32-bit output, without saturation'},
        engine_library_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest() if engine else None,
        engine_exact_cases={name: counts[name] for name in ('add','subtract','multiply')} if engine else {},
        exclusions=['Producer reachability of raw NaN/infinity/denormal/overflow patterns',
                    'Full spatial mutation after bounds construction',
                    'General simulation trajectories and trigonometry',
                    'Independent derivation of reciprocal table values; reference reads authoritative binary constants',
                    'Non-power-of-two map scales are not asserted to be producer-reachable'])
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
