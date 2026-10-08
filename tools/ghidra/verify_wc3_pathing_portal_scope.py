#!/usr/bin/env python3
"""Execute original16ec00; control only its16ecc0 placement boundary.

This checks captured-record lifetime, argument forwarding and return/SEH
restoration. It does not claim to execute placement geometry or exceptions.
"""
import argparse
import hashlib
import itertools
import json
import struct
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def validate_report(report):
    matrix = set(itertools.product(range(2), range(4), (0, 1, 7), range(2)))
    rows = report['cases']
    if not report['passed'] or report['binary_sha256'] != SHA or report['case_count'] != 48:
        raise ValueError('incomplete portal scope evidence')
    if len(rows) != 48 or {(r['result'], r['cls'], r['outer'], r['absent']) for r in rows} != matrix:
        raise ValueError('portal exit matrix differs')
    for row in rows:
        held = row['outer'] + (not row['absent'])
        if row['held'] != held or row['final_counter'] != row['outer']:
            raise ValueError('portal counter leaked or was not held')
        if row['policy'] != [row['cls'] * 2, 32, 24, 6, 0, 0, 2, 0]:
            raise ValueError('portal callback/policy differs')
        if row['point'] != ([51.5, 17.5] if row['result'] else [50.25, 16.75]):
            raise ValueError('portal boundary result differs')
    return report


def verify(binary):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX
    raw = binary.read_bytes()
    if hashlib.sha256(raw).hexdigest() != SHA:
        raise ValueError('unsupported original executable')
    pe = struct.unpack_from('<I', raw, 60)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', raw, opt + o)[0] for o in (28, 56))
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(base, (size + 4095) & ~4095)
    for i in range(struct.unpack_from('<H', raw, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', raw, pe + 20)[0] + 40 * i
        va, n, offset = struct.unpack_from('<III', raw, section + 12)
        if n:
            u.mem_write(base + va, raw[offset:offset + n])
    u.mem_map(0, 4096)
    u.mem_map(0x10000000, 0x10000)
    u.mem_map(0x20000000, 0x10000)
    mover, record, path, point = 0x10000000, 0x10001000, 0x10002000, 0x10003000
    stack, stop = 0x20008000, 0x30000000

    def write(address, *words):
        u.mem_write(address, struct.pack('<' + 'I' * len(words), *words))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, u.mem_read(address, 4 * count)))

    def floats(*values):
        return list(struct.unpack('<' + 'I' * len(values), struct.pack('<' + 'f' * len(values), *values)))

    source, admitted = floats(50.25, 16.75), floats(51.5, 17.5)
    state = {}

    def hook(uc, address, size, data):
        if address != 0x6f16ecc0:
            return
        sp = uc.reg_read(UC_X86_REG_ESP)
        args = read(sp + 4, 10)
        if uc.reg_read(UC_X86_REG_ECX) != point:
            raise ValueError('portal point register differs')
        if read(uc.reg_read(UC_X86_REG_EDX), 4) != [source[1], source[0], source[1], source[0]]:
            raise ValueError('portal bounds differ')
        if read(args[0]) != floats(.25 + state['cls'] * .5) or read(args[1]) != [0x02000002]:
            raise ValueError('footprint/query-mask pointer order differs')
        state['held'] = read(record + 0x40)[0]
        state['policy'] = args[2:]
        if state['result']:
            write(point, *admitted)
        uc.reg_write(UC_X86_REG_EAX, state['result'])
        uc.reg_write(UC_X86_REG_EIP, read(sp)[0])
        uc.reg_write(UC_X86_REG_ESP, sp + 44)

    u.hook_add(UC_HOOK_CODE, hook)
    rows = []
    for result, cls, outer, absent in itertools.product(range(2), range(4), (0, 1, 7), range(2)):
        state.update(result=result, cls=cls)
        state.pop('held', None)
        write(mover + 0x90, *floats(.25 + cls * .5))
        write(mover + 0x98, 0 if absent else record)
        write(mover + 0xa8, path)
        write(path + 0x88, cls << 30)
        write(path + 0x9c, 0x02000002)
        write(record + 0x40, outer)
        write(record + 0x20, 50, 16, 51, 17)
        write(point, *source)
        write(0, 0)
        write(stack, stop, point, 32, 24, 6)
        u.reg_write(UC_X86_REG_ESP, stack)
        u.reg_write(UC_X86_REG_ECX, mover)
        u.emu_start(0x6f16ec00, stop, count=10000)
        if u.reg_read(UC_X86_REG_EIP) != stop or u.reg_read(UC_X86_REG_ESP) != stack + 20:
            raise ValueError('portal RET10 ABI differs')
        if read(0) != [0] or read(record + 0x20, 4) != [50, 16, 51, 17]:
            raise ValueError('portal SEH/record bounds changed')
        if u.reg_read(UC_X86_REG_EAX) != result:
            raise ValueError('portal did not retain placement result')
        rows.append(dict(result=result, cls=cls, outer=outer, absent=absent,
                         held=state['held'], policy=state['policy'],
                         final_counter=read(record + 0x40)[0],
                         point=list(struct.unpack('<ff', u.mem_read(point, 8)))))
    return validate_report(dict(passed=True, binary_sha256=SHA, case_count=len(rows), cases=rows,
        scope='Complete original16ec00; controlled16ecc0 placement verdict/output only. Captured record, four footprints, null self, three outer depths, both exits and SEH/RET10; no geometry or thrown exception claim.'))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--fixture', type=Path)
    args = ap.parse_args()
    actual = verify(args.binary)
    if args.fixture and actual != json.loads(args.fixture.read_text()):
        raise ValueError('frozen portal scope differs')
    args.output.write_text(json.dumps(actual, indent=2) + '\n')
    print(json.dumps({k: v for k, v in actual.items() if k != 'cases'}))


if __name__ == '__main__':
    main()
