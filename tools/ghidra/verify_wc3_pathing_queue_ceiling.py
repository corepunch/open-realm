#!/usr/bin/env python3
"""Run retail693490 with supplied canonical identities and a controlled event sink.

Original admission, identity resolution, predecessor linking, head/tail and
count writes run unmodified. The unit's virtual event receiver is controlled;
this is not a factory, gameplay callback, or public501-command lifetime test.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def verify(binary):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_ECX
    raw = Path(binary).read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA:
        raise ValueError('unsupported retail binary')
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', raw, opt + n)[0] for n in (28, 56))
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(base, (size + 4095) & ~4095)
    u.mem_write(base, raw[:struct.unpack_from('<I', raw, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', raw, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', raw, pe + 20)[0] + 40 * i
        va, n, off = struct.unpack_from('<III', raw, section + 12)
        if n:
            u.mem_write(base + va, raw[off:off + n])
    u.mem_map(0, 4096)
    u.mem_map(0x10000000, 0x40000)
    u.mem_map(0x20000000, 0x10000)
    u.mem_map(0x30001000, 4096)
    unit, registry, slots, vt = 0x10000000, 0x10001000, 0x10002000, 0x10006000
    stack, stop, sink = 0x20008000, 0x30000000, 0x30001000
    new, internal = 700, 701
    end = [0xffffffff, 0xffffffff]

    def write(address, *words):
        u.mem_write(address, struct.pack('<' + 'I' * len(words), *(v & 0xffffffff for v in words)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, u.mem_read(address, count * 4)))

    def payload(index):
        return 0x10010000 + index * 0x100

    def identity(index):
        return [index, index + 100]

    events = []

    def observe(uc, address, size, data):
        if address == sink:
            sp = uc.reg_read(UC_X86_REG_ESP)
            event = read(sp + 4)[0]
            events.append(read(event + 8, 2))
            uc.reg_write(UC_X86_REG_EIP, read(sp)[0])
            uc.reg_write(UC_X86_REG_ESP, sp + 8)

    u.hook_add(UC_HOOK_CODE, observe)
    rows = []
    for repeat in range(2):
        for count in (0, 1, 499, 500, 501, 502):
            for flags in (0, 0x100):
                u.mem_write(0x10000000, bytes(0x40000))
                write(0, 0)
                write(0x6fd68610, registry)
                write(registry + 0xc, slots)
                write(registry + 0x18, 1024, 1024)
                write(unit, vt)
                write(vt + 0x10, sink)
                for index in list(range(count)) + [new, internal]:
                    obj = payload(index)
                    wrapper = obj + 0x80
                    write(slots + index * 8, -2, wrapper)
                    write(wrapper + 0xc, 0x2b61676c)
                    write(wrapper + 0x14, *identity(index))
                    write(wrapper + 0x54, obj)
                    write(obj + 4, 1)
                    write(obj + 0xc, *identity(index))
                    write(obj + 0x2c, *(identity(index + 1) if index < count - 1 else end))
                write(unit + 0x174, *identity(internal))
                write(unit + 0x19c, *(identity(0) if count else end))
                write(unit + 0x1a8, *(identity(count - 1) if count else end))
                write(unit + 0x1b4, count)
                write(unit + 0x5c, flags)
                events.clear()
                write(stack, stop, payload(new))
                u.reg_write(UC_X86_REG_ESP, stack)
                u.reg_write(UC_X86_REG_ECX, unit)
                u.emu_start(0x6f693490, stop, count=100000)
                if u.reg_read(UC_X86_REG_EIP) != stop or u.reg_read(UC_X86_REG_ESP) != stack + 8 or read(0) != [0]:
                    raise ValueError('retail append ABI/unwind differs')
                row = dict(count=count, flags=flags, final_count=read(unit + 0x1b4)[0],
                           head=read(unit + 0x19c, 2), tail=read(unit + 0x1a8, 2),
                           previous_next=read(payload(count - 1) + 0x2c, 2) if count else None,
                           new_next=read(payload(new) + 0x2c, 2),
                           events=[[code, identity(new)] for code, obj in events if obj == payload(new)])
                admitted = flags == 0 and count < 501
                expected = dict(count=count, flags=flags, final_count=count + admitted,
                                head=identity(0) if count else identity(new) if admitted else end,
                                tail=identity(new) if admitted else identity(count - 1) if count else end,
                                previous_next=identity(new) if count and admitted else end if count else None,
                                new_next=end, events=[[0xd02a5, identity(new)]] if admitted else [])
                if row != expected or len(events) != int(admitted):
                    raise ValueError((row, expected))
                rows.append(row)
    if rows[:12] != rows[12:]:
        raise ValueError('retail repeats differ')
    return dict(passed=True, binary_sha256=SHA, case_count=len(rows), cases=rows,
                scope='Complete original693490 with supplied canonical order/task identities and controlled virtual event receiver. No factory or public long-FIFO trajectory claim.')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--fixture', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    result = verify(args.binary)
    if args.fixture and json.loads(args.fixture.read_text()) != result:
        raise ValueError('frozen retail ceiling fixture differs')
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"PASS: {result['case_count']} retail append ceiling cases")


if __name__ == '__main__':
    main()
