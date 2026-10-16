#!/usr/bin/env python3
"""Execute original Blink notification window and Move target validation.

Original4c95c0,651010 and5fb940 bodies execute unmodified. Position admission,
event delivery, dead/type virtual queries and visibility
are controlled boundary stand-ins; this is not a full spell/movement oracle.
Existing public Frida archives separately prove Blink producer reachability.
"""
import argparse
import hashlib
import itertools
import json
import struct
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def verify(binary):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_ECX
    raw = binary.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA:
        raise ValueError('unsupported original executable')
    pe = struct.unpack_from('<I', raw, 60)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', raw, opt + off)[0] for off in (28, 56))
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(base, (size + 4095) & ~4095)
    for i in range(struct.unpack_from('<H', raw, pe + 6)[0]):
        at = opt + struct.unpack_from('<H', raw, pe + 20)[0] + 40 * i
        va, n, offset = struct.unpack_from('<III', raw, at + 12)
        if n:
            u.mem_write(base + va, raw[offset:offset+n])
    u.mem_map(0, 4096)
    u.mem_map(0x10000000, 0x10000)
    u.mem_map(0x20000000, 0x10000)
    u.mem_map(0x30000000, 4096)
    actor, ability, vtable, relocate, dead_query = (0x10000000, 0x10001000, 0x10002000, 0x30000100, 0x30000200)
    owner_query, event_sink = 0x30000300, 0x30000400
    stack, stop = 0x20008000, 0x30000000
    policy, trace = {}, []

    def write(at, *words):
        u.mem_write(at, struct.pack('<'+'I'*len(words), *(v & 0xffffffff for v in words)))

    def word(at):
        return struct.unpack('<I', u.mem_read(at, 4))[0]

    def ret(purge=0, value=None):
        esp = u.reg_read(UC_X86_REG_ESP)
        if value is not None:
            u.reg_write(UC_X86_REG_EAX, value)
        u.reg_write(UC_X86_REG_EIP, word(esp))
        u.reg_write(UC_X86_REG_ESP, esp + 4 + purge)

    def hook(uc, address, size, _):
        esp = u.reg_read(UC_X86_REG_ESP)
        if address == 0x6f652270:  # Terrain/support-height query; no window writes.
            ret(value=0)
        elif address == 0x6f68ecd0:  # Stop pre-relocation work.
            trace.append(['prepare', word(actor+0x20)])
            ret()
        elif address == relocate:  # Position admission/commit boundary.
            trace.append(['relocate', word(actor+0x20), word(word(esp+4)), word(word(esp+8))])
            ret(40)
        elif address == owner_query:
            ret(value=0)
        elif address == event_sink:  # Actual651010 packet at the virtual receiver.
            packet = word(esp+4)
            trace.append(['notify', word(actor+0x20), word(packet+8), word(packet+12),
                          word(packet+16), word(packet+20)])
            if policy.get('event_flags') is not None:
                write(actor+0x20, policy['event_flags'])
            ret(4)
        elif address == 0x6f6510b0:  # Post-notification publication.
            trace.append(['finish', word(actor+0x20)])
            ret()
        elif address == dead_query:
            ret(value=policy['dead'])
        elif address == 0x6f1d73b0:  # Cast widget to optional retained unit bridge.
            write(u.reg_read(UC_X86_REG_ECX), actor if policy['unit'] else 0)
            ret(4)
        elif address == 0x6f66fdd0:  # Controlled detection/fog result.
            trace.append(['visibility', word(esp+8), word(esp+12)])
            ret(12, policy['visible'])

    u.hook_add(UC_HOOK_CODE, hook)
    write(actor, vtable, 10)
    write(ability+0x30, actor)
    write(vtable+0x180, relocate)
    write(vtable+0x13c, dead_query)
    write(vtable+0xec, owner_query)
    write(vtable+0x10, event_sink)
    write(ability+0xf8, 0x44440000)
    write(ability+0x100, 0x44840000)

    def run(entry, *args):
        write(stack, stop, *args)
        u.reg_write(UC_X86_REG_ESP, stack)
        u.reg_write(UC_X86_REG_ECX, ability)
        u.emu_start(entry, stop, count=100000)
        if u.reg_read(UC_X86_REG_EIP) != stop or u.reg_read(UC_X86_REG_ESP) != stack+4+4*len(args):
            raise ValueError('original ABI or bounded execution differs')
        return u.reg_read(UC_X86_REG_EAX)

    windows = []
    for flags, event_flags in itertools.product([0, 1, 0x800000, 0x20000406, 0xffffffff],
                                                [None, 1, 0x800001, 0x20001445, 0xffffffff]):
        policy.clear(); policy['event_flags'] = event_flags
        trace.clear(); write(actor+0x20, flags)
        run(0x6f4c95c0, 0x13579bdf, 0x2468ace0)
        after = (flags | 0x800000) if event_flags is None else event_flags
        expected = [['prepare', flags], ['relocate', flags, 0x44440000, 0x44840000],
                    ['notify', flags | 0x800000, 0xd01a4, actor, 0, 0], ['finish', after & ~0x800000]]
        if trace != expected or word(actor+0x20) != after & ~0x800000:
            raise ValueError('Blink notification ordering/window differs')
        windows.append(dict(flags=flags, event_flags=event_flags, trace=trace.copy(), final=word(actor+0x20)))
    validations = []
    for is_unit, dead, hidden, transient, loaded, visible in itertools.product([0,1], repeat=6):
        policy.clear(); policy.update(unit=is_unit, dead=dead, visible=visible)
        trace.clear(); write(actor+0x20, hidden | (transient << 23)); write(actor+0x5c, loaded << 4)
        write(actor+4, 10)
        result = run(0x6f5fb940, actor)
        expected = 0xdd if dead else (0xaa if hidden else 0) if not is_unit else (
            (0xa9 if loaded else 0xaa) if hidden and not transient else (0 if visible else 0xdd))
        queried = bool(not dead and is_unit and (not hidden or transient))
        if result != expected or trace != ([['visibility', 0, 4]] if queried else []):
            raise ValueError('Move hidden/transient/dead/visibility validation differs: %r result=%#x expected=%#x trace=%r' % ((is_unit,dead,hidden,transient,loaded,visible),result,expected,trace))
        validations.append(dict(unit=is_unit, dead=dead, hidden=hidden, transient=transient,
                                loaded=loaded, visible=visible, result=result, queries=trace.copy()))
    policy.update(unit=1, dead=0, visible=1)
    if run(0x6f5fb940, 0) != 0xdd:
        raise ValueError('null-target validation differs')
    return dict(passed=True, binary_sha256=SHA, window_cases=windows, validation_cases=validations,
                cases=len(windows)+len(validations)+1,
                boundaries=['position admission', 'event delivery', 'dead/type virtual queries', 'visibility result'])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--fixture', type=Path)
    args = ap.parse_args()
    try:
        actual = verify(args.binary)
        if args.fixture and actual != json.loads(args.fixture.read_text()):
            raise ValueError('original results differ from frozen fixture')
        result = dict(status='bounded-original-blink-validation', **actual)
    except (ValueError, KeyError, OSError) as error:
        result = dict(status='failed', passed=False, error=str(error))
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('window_cases','validation_cases')}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
