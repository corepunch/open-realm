#!/usr/bin/env python3
"""Execute retail's parent-cell reducer against an exhaustive classification matrix.

Requires Unicorn and a local, matching game.dll. No retail bytes are distributed.
This checks the recovered contract against the original x86, not an OpenRealm port.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine-library',type=Path)
    args = parser.parse_args()
    engine=ctypes.CDLL(str(args.engine_library.resolve()))if args.engine_library else None
    exact=0
    if engine:engine.pathing_gate_parent.argtypes=[ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint8),ctypes.POINTER(ctypes.c_uint8)]
    binary = args.binary.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        parser.error('unsupported binary; requires game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + offset)[0] for offset in (28, 56))
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (size + 4095) & ~4095)
    machine.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + 40 * i
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            machine.mem_write(base + va, binary[offset:offset + count])
    machine.mem_map(0x10000000, 0x10000)
    machine.mem_map(0x20000000, 0x10000)
    parent, childmap, cells, stack, stop = 0x10000000, 0x10000100, 0x10000200, 0x20008000, 0x30000000

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    write(childmap + 0x28, cells)
    cases, failures = 0, []
    # Every legal classification, every special-child marker, every traversal lane,
    # and each edge truncation. Missing children behave as blocked children.
    for width, height in itertools.product((1, 2), repeat=2):
        write(childmap + 0x3c, width, height)
        for shift in (0, 2, 4, 6):
            mask = 0xc0000000 >> shift
            initial = 0x3f5abeef & ~mask
            for states in itertools.product((0, 1, 2), repeat=4):
                for markers in itertools.product((0, 1), repeat=4):
                    effective = []
                    for y, x in itertools.product(range(2), repeat=2):
                        index = y * 2 + x
                        if x >= width or y >= height:
                            effective.append(1)
                            continue
                        state, marker = states[index], markers[index]
                        word = (state << (30 - shift)) | (marker << 16) | 0x789a
                        write(cells + (y * width + x) * 8, 0x12345678, word)
                        effective.append(2 if marker and state == 0 else state)
                    expected = 0 if all(s == 0 for s in effective) else 1 if all(s == 1 for s in effective) else 2
                    write(parent, 0x13579bdf, initial)
                    write(stack, stop, parent, childmap, shift, 0, 0)
                    machine.reg_write(UC_X86_REG_ESP, stack)
                    machine.emu_start(0x6f15d1c0, stop, count=1000)
                    if machine.reg_read(UC_X86_REG_EIP) != stop:
                        raise RuntimeError('retail reducer exceeded instruction budget')
                    stamp, actual = struct.unpack('<II', machine.mem_read(parent, 8))
                    wanted = initial | (expected << (30 - shift))
                    if engine:
                        compact_states=[states[y*2+x]for y in range(height)for x in range(width)]
                        compact_markers=[markers[y*2+x]for y in range(height)for x in range(width)]
                        result=engine.pathing_gate_parent(width,height,(ctypes.c_uint8*len(compact_states))(*compact_states),(ctypes.c_uint8*len(compact_markers))(*compact_markers))
                        if result!=((actual>>(30-shift))&3):raise RuntimeError('C marker-aware parent reducer differs')
                        exact+=1
                    cases += 1
                    if stamp != 0x13579bdf or actual != wanted:
                        failures.append({'dimensions': [width, height], 'shift': shift, 'states': states,
                                         'markers': markers, 'actual': actual, 'expected': wanted, 'stamp': stamp})
                        if len(failures) >= 20:
                            raise RuntimeError('20 reducer mismatches; inspect the recovered contract')
    report = {'binary_sha256': digest, 'function': '6f15d1c0', 'cases': cases,
              'mismatches': failures,'engine_exact_cases':exact, 'scope': 'valid 00/01/10 classes; byte +6 markers; clipped 2x2 children; metadata preservation'}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{cases} retail parent-cell cases; {len(failures)} mismatches')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
