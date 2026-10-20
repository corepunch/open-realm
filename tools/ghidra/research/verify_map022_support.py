#!/usr/bin/env python3
"""MAP-02.2 original-code oracle: unit support height and point support helpers.

Executes original game.dll 1.27.1.7085 x86 under Unicorn:
  6f66d780  CUnit vtable+e4 support Z getter (with original 6f68c190 structure predicate)
  6f78d1e0  terrain/walkable-deck Z  (terrain 7437c0, deck 782a80 replaced by data stubs)
  6f64f7e0  GetLocationZ core       (original 78d1e0 + water stub 78bd60)
  6f64eca0  deep-water predicate    (04e090 terrain point-mask replaced by a one-cell stub)
Stubs only supply callee *outputs* (x87 ST0 via FLD from a slot, EAX, out-pointers);
all branch/selection/arithmetic logic is the original code. Each case is compared with
the inferred rule in this file (the rule the engine should port).
"""
import argparse, hashlib, itertools, json, struct
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
F32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--report', type=Path, required=True)
    args = ap.parse_args()
    binary = args.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest() != SHA:
        ap.error('requires game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]; opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + o)[0] for o in (28, 56))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (size + 4095) & ~4095)
    uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        sec = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, raw_size, raw = struct.unpack_from('<III', binary, sec + 12)
        if raw_size:
            uc.mem_write(base + va, binary[raw:raw + raw_size])
    uc.mem_map(0x10000000, 0x100000); uc.mem_map(0x20000000, 0x10000)
    UNIT, VT, PT, OUT, SLOTS, STOP, RES = 0x10000000, 0x10001000, 0x10002000, 0x10002100, 0x10003000, 0x10004000, 0x10002200
    STACK = 0x2000f000
    w32 = lambda a, v: uc.mem_write(a, struct.pack('<I', v & 0xffffffff))
    wf = lambda a, v: uc.mem_write(a, struct.pack('<f', v))
    r32 = lambda a: struct.unpack('<I', uc.mem_read(a, 4))[0]
    rf = lambda a: struct.unpack('<f', uc.mem_read(a, 4))[0]
    # Return trampoline: FSTP dword [RES]; then stop marker.
    uc.mem_write(STOP, b'\xd9\x1d' + struct.pack('<I', RES) + b'\x90')
    state = {}
    stubs = {}

    def stub(addr, kind, ret, slot):
        code = (b'\xd9\x05' if kind == 'st0' else b'\xa1') + struct.pack('<I', slot)
        code += (b'\xc2' + struct.pack('<H', ret)) if ret else b'\xc3'
        uc.mem_write(addr, code)
        stubs[addr] = slot

    def esp_arg(n):
        return r32(uc.reg_read(UC_X86_REG_ESP) + 4 + 4 * n)

    calls = []

    def on_code(machine, address, _size, _data):
        if address not in stubs:
            return
        slot = stubs[address]
        ecx, edx = machine.reg_read(UC_X86_REG_ECX), machine.reg_read(UC_X86_REG_EDX)
        s = state
        if address == 0x6f78d1e0:                      # fastcall ECX layer, EDX out*, x, y, flag
            layer = ecx - (1 << 32) if ecx & 0x80000000 else ecx
            flag = esp_arg(2)
            terrain = s['layer3'] if layer == -3 else s['terrain']
            deckable = layer in (-1, -2) or flag != 0
            z, on = terrain, 0
            if deckable and s['deck'] is not None and s['deck'] > terrain:
                z, on = s['deck'], 1
            wf(slot, z)
            if edx:
                w32(edx, on)
            calls.append(('78d1e0', layer, flag))
        elif address == 0x6f78bd60:                    # fastcall ECX point*, EDX out* -> EAX
            w32(slot, 1 if s['water'] is not None else 0)
            if s['water'] is not None and edx:
                wf(edx, s['water'])
            calls.append(('78bd60',))
        elif address == 0x6f66b1b0:                    # thiscall unit, point*, out*; RET 8
            wf(slot, s['multi'])
            out = esp_arg(1)
            if out:
                w32(out, s['multi_bridge'])
            calls.append(('66b1b0',))
        elif address == 0x6f69a9d0:
            wf(slot, s['building']); calls.append(('69a9d0',))
        elif address == 0x6f68f390:
            wf(slot, s['fly']); calls.append(('fly',))
        elif address == 0x6f054530:
            w32(slot, s['resolved']); calls.append(('054530',))
        elif address == 0x6f04e090:                    # fastcall &x,&y, stack mask, bridge
            mask = esp_arg(0)
            w32(slot, 1 if s['cell'] & mask else 0); calls.append(('04e090', mask))

    uc.hook_add(UC_HOOK_CODE, on_code)
    for i, (addr, kind, ret) in enumerate([(0x6f78d1e0, 'st0', 0xc), (0x6f78bd60, 'eax', 0), (0x6f66b1b0, 'st0', 8),
                                           (0x6f69a9d0, 'st0', 4), (0x6f68f390, 'st0', 0), (0x6f054530, 'eax', 0),
                                           (0x6f04e090, 'eax', 8)]):
        stub(addr, kind, ret, SLOTS + 16 * i)
    # Vtable: slot e8 -> original fly getter address (stubbed above).
    w32(UNIT, VT); w32(VT + 0xe8, 0x6f68f390)

    def call(entry, ecx, stack_args, edx=0, float_ret=True):
        calls.clear()
        sp = STACK - 0x100
        uc.mem_write(sp, struct.pack('<I', STOP) + b''.join(struct.pack('<I', a & 0xffffffff) for a in stack_args))
        uc.reg_write(UC_X86_REG_ESP, sp); uc.reg_write(UC_X86_REG_ECX, ecx); uc.reg_write(UC_X86_REG_EDX, edx)
        end = STOP + 6 if float_ret else STOP
        uc.emu_start(entry, end, count=100000)
        assert uc.reg_read(UC_X86_REG_EIP) == end
        assert uc.reg_read(UC_X86_REG_ESP) == sp + 4 + 4 * len(stack_args), hex(entry)  # callee cleanup
        return rf(RES) if float_ret else uc.reg_read(UC_X86_REG_EAX)

    fbits = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]
    report = dict(binary_sha256=SHA, functions={}, mismatches=[])

    # ---------------- 78d1e0: original body; only its callees' outputs are supplied.
    stubs_78d1e0 = stubs.pop(0x6f78d1e0)

    # Restore original 78d1e0 bytes and stub its callees instead.
    def text(va, n):
        for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
            sec = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
            vsz, vva, rsz, raw = struct.unpack_from('<IIII', binary, sec + 8)
            if base + vva <= va < base + vva + rsz:
                return binary[raw + va - base - vva: raw + va - base - vva + n]
    uc.mem_write(0x6f78d1e0, text(0x6f78d1e0, 0x90))
    T_SLOT, D_SLOT = SLOTS + 0x200, SLOTS + 0x210
    # 78e790 (fastcall, returns terrain singleton in EAX) -> MOV EAX,imm; RET
    uc.mem_write(0x6f78e790, b'\xb8' + struct.pack('<I', 0x10005000) + b'\xc3')
    # 7437c0 thiscall(point*, layer) RET 8 -> ST0 per layer
    uc.mem_write(0x6f7437c0, b'\xd9\x05' + struct.pack('<I', T_SLOT) + b'\xc2\x08\x00')
    # 78e810 returns deck singleton; 782a80 cdecl-like(point*, out*) -> EAX hit, *out = deck
    uc.mem_write(0x6f78e810, b'\xb8' + struct.pack('<I', 0x10005100) + b'\xc3')
    uc.mem_write(0x6f782a80, b'\xa1' + struct.pack('<I', D_SLOT) + b'\xc2\x08\x00')

    def on_inner(machine, address, _s, _d):
        s = state
        if address == 0x6f7437c0:
            layer = r32(machine.reg_read(UC_X86_REG_ESP) + 8)
            layer = layer - (1 << 32) if layer & 0x80000000 else layer
            wf(T_SLOT, s['layer3'] if layer == -3 else s['terrain'])
            s['inner'].append(('7437c0', layer))
        elif address == 0x6f782a80:
            esp = machine.reg_read(UC_X86_REG_ESP)
            out = r32(esp + 8)
            w32(D_SLOT, 1 if s['deck'] is not None else 0)
            if s['deck'] is not None:
                wf(out, s['deck'])
            s['inner'].append(('782a80',))
    uc.hook_add(UC_HOOK_CODE, on_inner, begin=0x6f7437c0, end=0x6f7437c0)
    uc.hook_add(UC_HOOK_CODE, on_inner, begin=0x6f782a80, end=0x6f782a80)

    def rule_78d1e0(layer, flag, terrain, layer3, deck):
        z = layer3 if layer == -3 else terrain
        if (layer in (-1, -2) or flag) and deck is not None and deck > z:
            return deck, 1
        return z, 0

    rows = []
    for layer, flag, terrain, deck in itertools.product([-1, -2, -3, 0, 5], [0, 1], [-192.0, 0.0, 128.0],
                                                        [None, -300.0, 0.0, 84.04711151123047]):
        state.update(terrain=terrain, layer3=96.0, deck=deck, inner=[])
        w32(OUT, 0xdead)
        z = call(0x6f78d1e0, layer, [fbits(1024.0), fbits(640.0), flag], edx=OUT)
        on = r32(OUT)
        ez, eon = rule_78d1e0(layer, flag, terrain, 96.0, deck)
        row = dict(layer=layer, flag=flag, terrain=terrain, deck=deck, z=z, bridge=on, inner=state['inner'])
        rows.append(row)
        if (F32(ez), eon) != (z, on):
            report['mismatches'].append(dict(fn='78d1e0', **row, expected=[ez, eon]))
    report['functions']['78d1e0'] = dict(cases=len(rows), rows=rows,
        rule='Z = terrain(layer) [7437c0; layer -3 selects the alternate field]; if (layer==-1 or -2 or flag!=0) and 782a80 reports a deck strictly higher, Z = deck and *EDX = 1, else *EDX = 0')

    # ---------------- 64f7e0: GetLocationZ core (original 78d1e0 + water stub)
    rows = []
    for terrain, deck, water in itertools.product([-192.0, 0.0], [None, 84.0], [None, -0.1, 200.0]):
        state.update(terrain=terrain, layer3=96.0, deck=deck, water=water, inner=[])
        z = call(0x6f64f7e0, 0xffffffff, [fbits(1024.0), fbits(640.0), 1], edx=0)
        ground = deck if deck is not None and deck > terrain else terrain
        ez = max(ground, water) if water is not None and water > ground else ground
        rows.append(dict(terrain=terrain, deck=deck, water=water, z=z))
        if F32(ez) != z:
            report['mismatches'].append(dict(fn='64f7e0', terrain=terrain, deck=deck, water=water, z=z, expected=ez))
    report['functions']['64f7e0'] = dict(cases=len(rows), rows=rows,
        rule='GetLocationZ = max(78d1e0(layer -1, flag 1), water surface when 78bd60 reports water and it is strictly higher)')

    # ---------------- 64eca0: deep-water predicate
    rows = []
    for cell in range(256):
        state.update(cell=cell)
        got = call(0x6f64eca0, 0, [fbits(1024.0), fbits(192.0)], float_ret=False)
        exp = 1 if (cell & 2) and not (cell & 0x40) else 0
        rows.append(got)
        if got != exp:
            report['mismatches'].append(dict(fn='64eca0', cell=cell, got=got, expected=exp))
    report['functions']['64eca0'] = dict(cases=256, ones=[c for c in range(256) if rows[c]],
        rule='deep = terrain point blocks walk (mask 02) AND does not block float (mask 40); 04e090 called with bridge arg 0')

    # ---------------- 66d780: unit support getter (78d1e0 stubbed again for clean inputs)
    uc.mem_write(0x6f78d1e0, b'\xd9\x05' + struct.pack('<I', stubs_78d1e0) + b'\xc2\x0c\x00')
    stubs[0x6f78d1e0] = stubs_78d1e0
    wf(PT, 1024.0); wf(PT + 4, 640.0)

    def rule_66d780(c):
        if c['structure'] and c['f280'] & 8:
            return c['building'], 'building'
        if not c['f280'] & 8:
            base, src = (c['deck'], 'deck') if c['deck'] is not None and c['deck'] > c['terrain'] else (c['terrain'], 'terrain')
        else:
            if not c['force'] and c['same_xy'] and not c['resolved']:
                return c['cached'], 'cached'
            base, src = c['multi'], 'multi-sample'
        fly = c['fly']
        if c['f5c_fly'] and c['ground200'] <= 0:
            d = c['layer3'] - base
            if c['max'] > 0.01:
                d = F32(F32(d * fly) / c['max'])
            return F32(fly + F32(d + base)), 'flyer-blend'
        z = base
        if c['move'] & 0x38 and c['water'] is not None and (c['move'] != 0x20 or c['f280'] & 0x20) and c['water'] > base:
            z, src = c['water'], 'water'
        return F32(fly + z), src

    rows = []
    grid = itertools.product([1, 2, 4, 8, 0x10, 0x20, 0x40, 0x18],   # Unit+1fc movement bits (0x18 = invalid multi-bit)
                             [0, 2, 8, 0x20, 0x28],                    # Unit+280
                             [0, 0x20000000, 0x10000, 0x10000 | 0x80], # Unit+5c (flying / structure / structure+bit7)
                             [0, 1],                                   # Unit+200 forced-ground counter
                             [None, -0.1, 300.0],                      # water surface
                             [None, 84.0],                             # deck
                             [0.0, 360.0],                             # fly height getter value
                             [0, 1])                                   # force
    for move, f280, f5c, g200, water, deck, fly, force in grid:
        c = dict(move=move, f280=f280, f5c_fly=bool(f5c & 0x20000000), structure=bool(f5c & 0x10000) and not (f5c & 0x80),
                 ground200=g200, water=water, deck=deck, fly=fly, force=force, terrain=-192.0, layer3=96.0, max=360.0,
                 multi=10.5, building=55.0, cached=-7.0, same_xy=True, resolved=0)
        state.update(c, multi_bridge=1)
        uc.mem_write(UNIT + 4, b'\0' * 0x300)
        w32(UNIT + 0x5c, f5c); w32(UNIT + 0x1fc, move); w32(UNIT + 0x200, g200); wf(UNIT + 0x210, 360.0)
        w32(UNIT + 0x280, f280); wf(UNIT + 0x284, 1024.0); wf(UNIT + 0x288, 640.0); wf(UNIT + 0x28c, -7.0); w32(UNIT + 0x224, 0)
        w32(OUT, 0xdead)
        z = call(0x6f66d780, UNIT, [PT, 0xffffffff, OUT, force])
        ez, src = rule_66d780(c)
        row = dict(move=move, f280=f280, f5c=f5c, ground200=g200, water=water, deck=deck, fly=fly, force=force,
                   z=z, source=src, out_flag=r32(OUT), calls=[x[0] for x in calls])
        rows.append(row)
        if F32(ez) != z:
            report['mismatches'].append(dict(fn='66d780', **row, expected=ez))
    report['functions']['66d780'] = dict(cases=len(rows), sources=dict(__import__('collections').Counter(r['source'] for r in rows)),
        abi='thiscall ECX unit; [esp+4] point*, [esp+8] layer (forwarded as ECX to 78d1e0), [esp+c] out on-bridge flag*, [esp+10] force; RET 0x10; result x87 ST0',
        rule='see rule_66d780 in this oracle', sample=rows[:40], rows=rows)
    report['total_cases'] = sum(v['cases'] for v in report['functions'].values() if isinstance(v, dict))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(dict(total=report['total_cases'], mismatches=len(report['mismatches']),
                          sources=report['functions']['66d780']['sources']), indent=1))
    if report['mismatches']:
        print(json.dumps(report['mismatches'][:5], indent=1))
        raise SystemExit(1)


if __name__ == '__main__':
    main()
