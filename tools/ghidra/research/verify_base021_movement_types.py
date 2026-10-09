#!/usr/bin/env python3
"""BASE-02.1 original-code oracle: authored movement-type parser and lane/mask tables.

Executes, unmodified, under Unicorn:
  * game.dll 6f685340 (movetp string -> movement bits) through its real import
    Storm.dll ordinal 509 (SStrCmpI) and Storm's real import msvcr120 _strnicmp;
  * game.dll 6f685e30 (movement bits, EDX selector -> query / category byte);
  * game.dll 6f685db0 (movement bits -> movement-class flags, published >>1);
  * the profile-builder slice 6f66c909..6f66c944 with only the SLK column getter
    6f6b3640 replaced by a stub that returns the supplied string pointer.
No game state, no other stubs. The CRT is mapped without DLL startup; its
ever-changed-locale global therefore keeps its static value 0, matching the
live captured value recorded by NUM-01.13 (retail-pathfinding-engine.md).
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

GAME_SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
STORM_SHA = '36339f69727f0f3dcb4431a9567b84f56007c331cfa3b183862af43e8314eb72'
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from wc3_shipped_crt import load_crt  # noqa: E402

NAMES = ['fly', 'hover', 'foot', 'horse', 'unbuild', 'float', 'amph', '_', '-', 'none', '']
EXTRA = ['FLY', 'Fly', 'fLy', 'HOVER', 'Hover', 'FOOT', 'Foot', 'HORSE', 'UNBUILD', 'Unbuild', 'FLOAT', 'Float',
         'AMPH', 'Amph', 'NONE', 'None', ' foot', 'foot ', 'foot\t', 'foo', 'foots', 'fl', 'flyy', 'amphibious',
         'boat', 'naval', 'ground', 'air', 'water', 'walk', 'build', 'foot,fly', 'fly,foot', 'foot fly', 'foot|fly',
         'hover,float', '0', '1', '2', '__', '--', ' ', 'n', 'nonE', 'unbuildable', 'unamph', 'unfloat',
         'f\xf6\xf6t', 'FOOT\x00junk']


def load_pe(uc, path, sha, base_expected=None):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != sha:
        raise SystemExit(f'unsupported {path.name}')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', data, opt + o)[0] for o in (28, 56))
    if base_expected is not None:
        assert base == base_expected
    uc.mem_map(base, (size + 4095) & ~4095)
    uc.mem_write(base, data[:struct.unpack_from('<I', data, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', data, pe + 6)[0]):
        sec = opt + struct.unpack_from('<H', data, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', data, sec + 12)
        if count:
            uc.mem_write(base + va, data[offset:offset + count])
    exp = struct.unpack_from('<I', data, opt + 96)[0]
    obase, nfun = struct.unpack('<II', uc.mem_read(base + exp + 16, 8))
    afun = struct.unpack('<I', uc.mem_read(base + exp + 28, 4))[0]
    ordinal = lambda n: base + struct.unpack('<I', uc.mem_read(base + afun + 4 * (n - obase), 4))[0]
    return base, ordinal


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX,
                                   UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_EDI, UC_X86_REG_EBP,
                                   UC_X86_REG_ESI)
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True, help='game.dll; Storm.dll and msvcr120.dll are siblings')
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--write-expected', type=Path)
    args = ap.parse_args()
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    load_pe(uc, args.binary, GAME_SHA, 0x6f000000)
    storm_base, storm_ord = load_pe(uc, args.binary.with_name('Storm.dll'), STORM_SHA, 0x15000000)
    crt = load_crt(uc, args.binary.with_name('msvcr120.dll'))
    sstrcmpi = storm_ord(509)
    assert sstrcmpi == 0x1503a5a0
    # Bind only the two imports on the executed path.
    uc.mem_write(0x6fa7c830, struct.pack('<I', sstrcmpi))
    uc.mem_write(0x15041254, struct.pack('<I', crt['exports']['_strnicmp']))
    uc.mem_map(0x20000000, 0x100000)
    stack, stop, strings = 0x20080000, 0x30000000, 0x20010000
    uc.mem_map(stop, 0x1000)

    def run(entry, ecx, edx=0, stack_args=(), regs=None):
        uc.mem_write(stack, struct.pack('<' + 'I' * (1 + len(stack_args)), stop, *stack_args))
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, ecx & 0xffffffff)
        uc.reg_write(UC_X86_REG_EDX, edx & 0xffffffff)
        for r, v in (regs or {}).items():
            uc.reg_write(r, v)
        uc.emu_start(entry, stop, count=5_000_000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop
        return uc.reg_read(UC_X86_REG_EAX), uc.reg_read(UC_X86_REG_ESP) - stack

    table = []
    for i in range(16):
        p, v = struct.unpack('<II', uc.mem_read(0x6fce6000 + 8 * i, 8))
        if not p:
            break
        table.append(dict(index=i, name=bytes(uc.mem_read(p, 32)).split(b'\0')[0].decode('latin1'), bits=v,
                          name_va=f'{p:08x}'))
    parse = []
    seen = set()
    for text in NAMES + EXTRA:
        if text in seen:
            continue
        seen.add(text)
        raw = text.encode('latin1')
        uc.mem_write(strings, raw + b'\0' * 8)
        bits, depth = run(0x6f685340, strings)
        assert depth == 4, 'fastcall plain RET'
        parse.append(dict(input=raw.hex(), text=text, bits=bits))
    null_bits, _ = run(0x6f685340, 0)
    lanes = []
    for bits in list(range(0x80)) + [0x80, 0x81, 0xff, 0x100, 0x7fffffff, 0x80000000, 0xffffffff]:
        row = dict(bits=bits)
        for edx in (0, 1, 2, 0x80000000, 0xffffffff):
            value, depth = run(0x6f685e30, bits, edx)
            assert depth == 4
            row[f'map_edx_{edx:x}'] = value
        cls, depth = run(0x6f685db0, bits, 0x5a5a5a5a)
        assert depth == 4
        row['class_flags'] = cls
        row['published_class'] = cls >> 1
        lanes.append(row)
    # Profile-builder slice with only the SLK getter stubbed.
    getter_calls = []
    row_base, frame = 0x20020000, 0x20070000

    def stub(machine, address, size, data):
        if address == 0x6f6b3640:
            esp = machine.reg_read(UC_X86_REG_ESP)
            ret, out = struct.unpack('<II', machine.mem_read(esp, 8))
            getter_calls.append(dict(this=machine.reg_read(UC_X86_REG_ECX), out=out))
            machine.reg_write(UC_X86_REG_EAX, data['string'])
            machine.reg_write(UC_X86_REG_ESP, esp + 8)
            machine.reg_write(UC_X86_REG_EIP, ret)
    state = {'string': 0}
    hook = uc.hook_add(UC_HOOK_CODE, stub, state, 0x6f6b3640, 0x6f6b3640)
    builder = []
    for text in [p['text'] for p in parse]:
        raw = text.encode('latin1')
        uc.mem_write(strings, raw + b'\0' * 8)
        state['string'] = strings
        uc.mem_write(row_base, b'\xcc' * 0x300)
        getter_calls.clear()
        uc.reg_write(UC_X86_REG_EBX, row_base)
        uc.reg_write(UC_X86_REG_EDI, 0x20030000)
        uc.reg_write(UC_X86_REG_EBP, frame)
        uc.reg_write(UC_X86_REG_ESP, frame - 0x100)
        uc.emu_start(0x6f66c909, 0x6f66c944, count=5_000_000)
        assert uc.reg_read(UC_X86_REG_EIP) == 0x6f66c944
        words = struct.unpack('<III', uc.mem_read(row_base + 0x1a8, 12))
        untouched = bytes(uc.mem_read(row_base, 0x1a8)) + bytes(uc.mem_read(row_base + 0x1b4, 0x300 - 0x1b4))
        assert untouched == b'\xcc' * len(untouched)
        assert len(getter_calls) == 1 and getter_calls[0]['this'] == 0x20030000 and getter_calls[0]['out'] == frame - 0x4c
        builder.append(dict(text=text, row_1a8_bits=words[0], row_1ac_category=words[1], row_1b0_query=words[2]))
    uc.hook_del(hook)
    for p, b in zip(parse, builder):
        assert p['bits'] == b['row_1a8_bits']
        lane = next((l for l in lanes if l['bits'] == p['bits']), None)
        assert lane['map_edx_1'] == b['row_1ac_category'] and lane['map_edx_0'] == b['row_1b0_query']
    nonzero = {l['bits']: (l['map_edx_0'], l['map_edx_1'], l['published_class']) for l in lanes
               if l['map_edx_0'] or l['map_edx_1'] or l['class_flags']}
    report = dict(game_sha256=GAME_SHA, storm_sha256=STORM_SHA, crt_sha256=crt['sha256'],
                  storm_sstrcmpi=f'{sstrcmpi:08x}', crt_strnicmp=f"{crt['exports']['_strnicmp']:08x}",
                  name_table=table, parse=parse, parse_null_ecx=null_bits, lanes=lanes,
                  builder_slice=builder, nonzero_lane_rows={f'{k:x}': v for k, v in nonzero.items()},
                  counts=dict(parse=len(parse), lane_inputs=len(lanes), lane_calls=len(lanes) * 6,
                              builder=len(builder)),
                  scope='Original 685340/685e30/685db0 and builder slice 66c909..66c944; only 6b3640 stubbed. '
                        'CRT default locale (ever-changed flag 0) as captured live in NUM-01.13.')
    text = json.dumps(report, indent=1) + '\n'
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(text)
    if args.write_expected:
        expected = dict(version=1, task='BASE-02.1', game_sha256=GAME_SHA,
                        name_table=[(t['name'], t['bits']) for t in table],
                        parse={p['text']: p['bits'] for p in parse},
                        query_by_bits={f'{l["bits"]:x}': l['map_edx_0'] for l in lanes if l['bits'] < 0x80},
                        category_by_bits={f'{l["bits"]:x}': l['map_edx_1'] for l in lanes if l['bits'] < 0x80},
                        class_flags_by_bits={f'{l["bits"]:x}': l['class_flags'] for l in lanes if l['bits'] < 0x80})
        args.write_expected.write_text(json.dumps(expected, indent=1) + '\n')
    print(json.dumps(dict(counts=report['counts'], nonzero=report['nonzero_lane_rows'],
                          parse={p['text']: p['bits'] for p in parse}), indent=None))


if __name__ == '__main__':
    main()
