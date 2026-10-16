#!/usr/bin/env python3
"""Original scalar-listener scheduling/evaluation for positive Apiv fades.

Runs complete 161c30/1618a0 bodies and their native scalar/clock consumers.
Only wrapper timer ownership and event delivery are stand-ins. Inputs are
controlled canonical properties, not claims of public producer reachability.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def verify_header(rows, header):
    actual = [[int(word, 0) for word in re.findall(r'(0x[0-9a-fA-F]+|\d+)u', line)]
              for line in header.read_text().splitlines() if line.startswith('    {')]
    expected = [[row['duration'], row['origin'], len(row['requests']),
                 *(word for request in row['requests'] for word in request), row['publication'] or 0]
                for row in rows]
    if actual != expected:
        raise ValueError('engine header differs from original fade words')


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--fixture', type=Path)
    ap.add_argument('--header', type=Path)
    args = ap.parse_args()
    raw = args.binary.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA:
        ap.error('unsupported original executable')
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
    owner, prop, listener, sink, vtable, value, scratch = (0x10000000 + i * 0x100 for i in range(7))
    stack, stop, event = 0x20008000, 0x30000000, 0x30000100
    clock = owner + 0x14
    emitted, periods = [], []

    def write(at, *words):
        u.mem_write(at, struct.pack('<'+'I'*len(words), *words))

    def word(at):
        return struct.unpack('<I', u.mem_read(at, 4))[0]

    def bits(x):
        return struct.unpack('<I', struct.pack('<f', x))[0]

    def ret(purge=0):
        esp = u.reg_read(UC_X86_REG_ESP)
        u.reg_write(UC_X86_REG_EIP, word(esp))
        u.reg_write(UC_X86_REG_ESP, esp + 4 + purge)

    def hook(uc, address, size, _):
        esp = uc.reg_read(UC_X86_REG_ESP)
        if address == 0x6f15d7a0:  # StopTimer: owned wrapper bookkeeping only.
            ret()
        elif address == 0x6f15d6f0:  # StartTimer: record input, do not replace arithmetic.
            periods.append(word(word(esp + 4)))
            ret(8)
        elif address == event:
            emitted.append(word(clock + 0x40))
            ret(4)
    u.hook_add(UC_HOOK_CODE, hook)

    def run(address, *args, ecx=0, edx=0, purge=0):
        write(stack, stop, *args)
        u.reg_write(UC_X86_REG_ESP, stack)
        u.reg_write(UC_X86_REG_ECX, ecx)
        u.reg_write(UC_X86_REG_EDX, edx)
        try:
            u.emu_start(address, stop, count=2000000)
        except Exception as error:
            raise ValueError('native execution failed at %#x (entry %#x)' %
                             (u.reg_read(UC_X86_REG_EIP), address)) from error
        if u.reg_read(UC_X86_REG_EIP) != stop or u.reg_read(UC_X86_REG_ESP) != stack + 4 + purge:
            raise ValueError('original ABI/budget differs at %#x' % address)
        return u.reg_read(UC_X86_REG_EAX)

    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from wc3_shipped_crt import load_crt
    crt = load_crt(u, args.binary.parent / 'msvcr120.dll')
    write(0x6fa7c4fc, crt['exports']['isdigit'])
    for address in (0x6f001dd0, 0x6f001a80, 0x6f001b80, 0x6f001cb0, 0x6f003d00):
        run(address)
    write(0x6fd53a48, owner)
    write(clock + 0x48, bits(300))
    write(listener + 0x40, prop)
    write(listener + 0x50, bits(1), 1)
    write(listener + 0x30, sink)
    write(sink, vtable)
    write(vtable + 0x20, event)
    rows = []
    for duration in (0.001, 0.01, 0.5, 2, 3.25, 5, 12.75, 301, 2000000):
        for start in (0, 17.25, 299.5):
            emitted.clear()
            periods.clear()
            write(value, bits(duration))
            run(0x6f0711e0, ecx=scratch, edx=value)
            slope = word(scratch)
            write(prop + 0x70, bits(start), 0)
            write(prop + 0x78, word(0x6fcd53a0), slope, bits(0), bits(1))
            write(clock + 0x40, bits(start), 0)
            run(0x6f1618a0, ecx=listener)
            chain = []
            for _ in range(100):
                if emitted:
                    break
                if not periods:
                    break  # Native slope guard can leave a positive fade without a request.
                delay = periods.pop()
                write(value, max(delay, bits(0.0001)))
                run(0x6f06fbb0, value, ecx=scratch, edx=clock + 0x40, purge=4)
                due = word(scratch)
                chain.append([word(clock + 0x40), delay, due])
                write(clock + 0x40, due)
                run(0x6f1618a0, ecx=listener)
            if len(emitted) > 1 or (chain and not emitted):
                raise ValueError('missing/duplicate fade publication')
            rows.append(dict(duration=bits(duration), origin=bits(start), slope=slope,
                             requests=chain, publication=emitted[0] if emitted else None))
    result = dict(status='verified-invisibility-fade', binary_sha256=SHA, passed=True, cases=len(rows), rows=rows,
                  constants=dict(initial=word(0x6fcd53a0), tolerance=word(0x6fd53a70),
                                 maximum_delay=word(0x6fd3c754)),
                  exclusions=['Controlled canonical inputs; full modifier construction and overlapping contributions excluded.',
                              'Timer ownership/free-list and event receiver are explicit stand-ins.',
                              'No epoch drain/rebase in this oracle; public clock integration has separate fixtures.'])
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    if args.fixture:
        expected = json.loads(args.fixture.read_text())
        if result != expected:
            raise ValueError('original fade words differ from frozen evidence')
    if args.header:
        verify_header(rows, args.header)
    print(json.dumps({k: result[k] for k in ('passed', 'cases', 'constants')}))


if __name__ == '__main__':
    main()
