#!/usr/bin/env python3
"""Verify bounded original WPM-byte and decoded image path-mask consumer loops.

No stubs or retail bytes. Parser/decompression/allocator calls are outside the
observed slices. Original loader loops execute against independent mask models.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import gzip
import struct
import sys
from pathlib import Path


SHA256 = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_INVALID, UcError
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EBX, UC_X86_REG_EAX, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path, help='compare production movement bits for all256 WPM bytes')
    parser.add_argument('--write-fixture', type=Path, help='freeze the actual original single-byte WPM outputs')
    parser.add_argument('--bridge-fixture',type=Path,help='authenticate file-backed bridge terrain authority and observer control')
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())) if args.engine_library else None
    if engine:
        engine.pathing_wpm_flags.argtypes = [ctypes.c_uint32]
        engine.pathing_wpm_flags.restype = ctypes.c_uint32
    binary = args.binary.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != SHA256:
        parser.error('unsupported binary; requires game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + offset)[0] for offset in (28, 56))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (size + 4095) & ~4095)
    uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            uc.mem_write(base + va, binary[offset:offset + count])
    uc.mem_map(0x10000000, 0x100000)
    uc.mem_map(0x20000000, 0x10000)
    owner, system, fine, game = 0x10000000, 0x10001000, 0x10002000, 0x10003000
    cells, xptr, yptr, rect = 0x10004000, 0x10008000, 0x10008010, 0x10008020
    maps = [0x10009000 + n * 0x100 for n in range(4)]
    storage = [0x10010000 + n * 0x10000 for n in range(4)]
    stack, stop = 0x20008000, 0x30000000

    def write(address, *values):
        uc.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def floats(address, *values):
        uc.mem_write(address, struct.pack('<' + 'f' * len(values), *values))

    def words(address, count):
        return list(struct.unpack('<' + 'I' * count, uc.mem_read(address, 4 * count)))

    def run(entry, ecx, *arguments, edx=0):
        write(stack, stop, *arguments)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        uc.emu_start(entry, stop, count=2000000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop, hex(entry)

    source, frame = 0x10070000, 0x20007000
    write(fine + 0x28, cells)
    invalid = []

    def invalid_memory(machine, access, address, size, value, data):
        invalid.append(dict(access=access, address=address, size=size,
                            instruction=hex(machine.reg_read(UC_X86_REG_EIP))))
        return False

    uc.hook_add(UC_HOOK_MEM_INVALID, invalid_memory)

    def wpm_mask(byte):
        mask = 0
        for bit, flags in [(2, 0x13000000), (4, 0x05000000), (8, 0x09000000),
                           (0x20, 0x20000000), (0x40, 0x41000000)]:
            if byte & bit:
                mask |= flags
        if byte & 0x80 or byte & 0x42 == 0x42:
            mask |= 0x81000000
        return mask

    def image_mask(pixel):
        # Decoder color naming is outside this slice; compare raw offsets +0..+3.
        return ((0x09000000 if pixel[0] else 0) |
                (0x05000000 if pixel[1] else 0) |
                (0x03000000 if pixel[2] else 0) |
                (0x20000000 if pixel[3] else 0))

    def execute(kind, pixels, source_size, map_size, prior, flip=0, fault=False):
        width, height = source_size
        mw, mh = map_size
        write(fine + 0x3c, mw, mh)
        count = mw * mh
        # Guard prefix/suffix and all untouched padding cells are part of compare.
        seed = [prior ^ (n & 0xffff) for n in range(count)]
        write(cells - 16, *([0x12345678] * 4), *seed, *([0x87654321] * 4))
        payload = bytes(pixels) if kind == 'wpm' else bytes(v for pixel in pixels for v in pixel)
        uc.mem_write(source, payload or b'\0')
        write(frame - 0x5c, width, height)
        write(frame - 0x64, 0)
        write(frame - 0x6c, flip)
        uc.reg_write(UC_X86_REG_EBP, frame)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ESI, fine)
        if kind == 'wpm':
            uc.reg_write(UC_X86_REG_EBX, source)
            begin, end = 0x6f04caba, 0x6f04cb4f
            masks = [wpm_mask(byte) for byte in pixels]
        else:
            uc.reg_write(UC_X86_REG_EAX, source)
            begin, end = 0x6f04cc3f, 0x6f04ccb9
            masks = [image_mask(pixel) for pixel in pixels]
        invalid.clear()
        try:
            uc.emu_start(begin, end, count=100000)
        except UcError:
            assert fault and len(invalid) == 1 and invalid[0]['address'] == 0, invalid
            return invalid[0]
        assert not fault, 'expected unsupported oversized-mask null write'
        assert uc.reg_read(UC_X86_REG_EIP) == end
        expected = seed[:]
        for index, mask in enumerate(masks):
            y, x = divmod(index, width)
            if kind == 'image' and flip:
                y = height - y - 1
            if x < mw and y < mh:
                expected[y * mw + x] |= mask
            else:
                assert mask == 0
        assert words(cells - 16, count + 8) == [0x12345678] * 4 + expected + [0x87654321] * 4
        assert bytes(uc.mem_read(source, len(payload))) == payload
        pointer_register = UC_X86_REG_ECX if kind == 'wpm' else UC_X86_REG_EDX
        # Nonempty loops advance exactly once per source pixel, independently of padding.
        if width and height:
            assert uc.reg_read(pointer_register) == source + len(payload)
        return words(cells, 1)[0]

    priors = [0, 0xffffff, 0x123456, 0x49001b76, 0xa50000ab, 0xffffffff]
    layouts = [((1, 1), (1, 1)), ((2, 3), (5, 4)), ((3, 2), (3, 2)), ((5, 3), (7, 5))]
    wpm_cases = image_cases = empty_cases = 0
    wpm_words = []
    for byte, prior, (src, dst) in itertools.product(range(256), priors, layouts):
        # Vary successive pixels as well as testing every uniform mask at 1x1.
        pixels = [(byte + 17 * n) & 255 for n in range(src[0] * src[1])]
        result = execute('wpm', pixels, src, dst, prior)
        if prior == 0 and src == dst == (1, 1):
            wpm_words.append(result)
            if engine and engine.pathing_wpm_flags(byte) & 0xc6 != (result >> 24) & 0xc6:
                raise RuntimeError("engine WPM movement bits differ for byte" + str(byte))
        wpm_cases += 1
    for pixel, prior, flip, (src, dst) in itertools.product(
            itertools.product([0, 1, 127, 255], repeat=4), priors, [0, 1], layouts):
        pixels = [tuple(pixel[(channel + n) % 4] for channel in range(4))
                  for n in range(src[0] * src[1])]
        execute('image', pixels, src, dst, prior, flip)
        image_cases += 1
    for kind, src, prior, flip in itertools.product(['wpm', 'image'], [(0, 0), (0, 3), (3, 0)], priors, [0, 1]):
        execute(kind, [], src, (3, 3), prior, flip)
        empty_cases += 1
    # Bound checks choose NULL outside the destination; they do not skip nonzero
    # mask writes. Confirm only the consumer's precondition, not live reachability.
    oversized = []
    for kind, flip in itertools.product(['wpm', 'image'], [0, 1]):
        harmless = [0x11] * 6 if kind == 'wpm' else [(0, 0, 0, 0)] * 6
        execute(kind, harmless, (3, 2), (2, 1), 0x49001b76, flip)
        hazardous = [2] * 6 if kind == 'wpm' else [(0, 0, 1, 0)] * 6
        result = execute(kind, hazardous, (3, 2), (2, 1), 0x49001b76, flip, True)
        oversized.append(dict(kind=kind, flip=flip, **result))
    report = dict(binary_sha256=digest, wpm_loop=['6f04caba', '6f04cb4f'],
                  image_loop=['6f04cc3f', '6f04ccb9'], wpm_cases=wpm_cases,
                  image_cases=image_cases, zero_dimension_cases=empty_cases,
                  wpm_bit_masks={'02': '13000000', '04': '05000000', '08': '09000000', '20': '20000000', '40': '41000000', '80_or_42_combination': '81000000'},
                  image_nonzero_byte_masks=['09000000', '05000000', '03000000', '20000000'],
                  oversized_consumer_fault_witnesses=oversized,
                  scope='Bounded original decoded-byte consumer loops, all 256 WPM masks, all 256 four-valued decoded-byte channel combinations, OR-preservation of prior terrain/occupancy, padded destination dimensions, row flip and zero dimensions. No parser, decompressor, archive or full loader. Oversized nonzero masks null-write in this isolated consumer; malformed-file reachability unproven.')
    if engine:
        report.update(engine_queries=len(wpm_words), engine_sha256=hashlib.sha256(args.engine_library.read_bytes()).hexdigest())
    if args.write_fixture:
        assert len(wpm_words) == 256
        args.write_fixture.write_text(json.dumps(dict(version=1, binary_sha256=digest,
            source_loop=['6f04caba', '6f04cb4f'], wpm_masks=wpm_words), indent=2) + '\n')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    if args.bridge_fixture:
        sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'frida'))
        from verify_wc3_map_load_trace import verify_bridge_terrain,verify_bridge_saved
        fixture=json.loads(args.bridge_fixture.read_text())
        bundle=json.loads(gzip.decompress((Path(__file__).parent/'fixtures/retail-bridge-terrain-inputs-1.27.json.gz').read_bytes()))
        report.update(verify_bridge_terrain(fixture,bundle))
        saved=json.loads((Path(__file__).parent/'fixtures/retail-bridge-terrain-ghidra-1.27.json').read_text())
        if saved['binary_sha256']!=digest:raise ValueError('bridge saved evidence binary differs')
        report['bridge_saved_functions']=verify_bridge_saved(saved)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
