#!/usr/bin/env python3
"""Complete original waiting Mover_StepRoute calls with retained fine inputs.

No replaced routines or executable bytes. A live nonzero countdown bypasses
search and preserves the caller destination; arrival, destination checking,
native subtraction, heading selection and stop/turn update all execute.
Acquisition-time waypoint selection and full crowd trajectories remain separate.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import struct
from pathlib import Path
from verify_wc3_pathing_numeric import bits, initialize_runtime_scalars

TARGET = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EDX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--fixture', type=Path)
    args = parser.parse_args()
    binary = args.binary.read_bytes()
    if hashlib.sha256(binary).hexdigest() != TARGET:
        parser.error('requires original game.dll 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    opt = pe + 24
    base, size = (struct.unpack_from('<I', binary, opt + n)[0] for n in (28, 56))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (size + 4095) & ~4095)
    uc.mem_write(base, binary[:struct.unpack_from('<I', binary, opt + 60)[0]])
    for i in range(struct.unpack_from('<H', binary, pe + 6)[0]):
        section = opt + struct.unpack_from('<H', binary, pe + 20)[0] + i * 40
        va, count, offset = struct.unpack_from('<III', binary, section + 12)
        if count:
            uc.mem_write(base + va, binary[offset:offset + count])
    uc.mem_map(0x10000000, 0x20000)
    uc.mem_map(0x20000000, 0x10000)
    stack, stop = 0x20008000, 0x30000000
    mover, path = 0x10001000, 0x10002000
    source, destination, speed, heading, arrived, held, changed = range(0x10003000, 0x10003070, 16)

    def write(address, *words):
        uc.mem_write(address, struct.pack('<' + 'I' * len(words), *(w & 0xffffffff for w in words)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, uc.mem_read(address, count * 4)))

    def run(entry, self, arguments, edx=0):
        write(stack, stop, *arguments)
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, self)
        uc.reg_write(UC_X86_REG_EDX, edx)
        uc.emu_start(entry, stop, count=100000)
        assert uc.reg_read(UC_X86_REG_EIP) == stop, 'original caller instruction budget exceeded'
        assert uc.reg_read(UC_X86_REG_ESP) == stack + 4 * (len(arguments) + 1)

    startup = initialize_runtime_scalars(uc, stack, stop)
    calls = {'advance': 0, 'heading': 0, 'update': 0}
    def observe(machine, address, size, userdata):
        calls[{0x6f165ae0:'advance', 0x6f16f630:'heading', 0x6f170880:'update'}[address]] += 1
    for entry in (0x6f165ae0, 0x6f16f630, 0x6f170880):
        uc.hook_add(UC_HOOK_CODE, observe, begin=entry, end=entry)
    engine = ctypes.CDLL(str(args.engine.resolve())) if args.engine else None
    if engine:
        engine.pathing_subtract.argtypes = [ctypes.c_uint32] * 2
        engine.pathing_subtract.restype = ctypes.c_uint32
        engine.pathing_heading_error.argtypes = [ctypes.c_uint32] * 3
        engine.pathing_heading_error.restype = ctypes.c_uint32
        engine.pathing_motion.argtypes = [ctypes.POINTER(ctypes.c_uint32)]
    sources = [(bits(8),bits(8)), (0x3fb7b01a,0x3fe4fca7), (0x41020001,0x4107ffff),
               (0x41b12345,0x417abcde)]
    targets = [(12,8), (12,24), (-12,24), (-12,-8), (12,-8), (40,24), (40,40)]
    rows = []
    for native, target, facing, turn, delay in itertools.product(
            sources, targets, (0,0.25,1,3,5,6), (0.125,0.5), (1,4,20)):
        target = [bits(v) for v in target]
        uc.mem_write(mover, bytes(0x100)); uc.mem_write(path, bytes(0x100))
        write(mover+4,1); write(mover+0xa8,path)
        write(mover+0xb0,bits(0.49),bits(0.25),bits(turn),bits(0.25))
        write(path+0x94,delay)
        for offset in (0x1c,0x24,0x2c):
            write(path+offset,*target)
        write(source,*native); write(destination,*target)
        write(speed,bits(2)); write(heading,bits(facing))
        write(arrived,0); write(held,0); write(changed,0)
        run(0x6f16fbd0,mover,[source,destination,speed,heading,arrived,held,changed,0,0])
        result = [read(speed)[0],read(heading)[0],read(path+0x94)[0]]
        assert result[0] == 0 and result[2] == delay-1
        assert [read(a)[0] for a in (arrived,held,changed)] == [0,0,0]
        assert read(source,2) == list(native) and read(destination,2) == target
        if engine:
            x = engine.pathing_subtract(target[0],native[0])
            y = engine.pathing_subtract(target[1],native[1])
            error = engine.pathing_heading_error(x,y,bits(facing))
            motion = (ctypes.c_uint32*7)(bits(2),bits(facing),error,0,bits(turn),bits(0.25),1)
            engine.pathing_motion(motion)
            assert list(motion)[:2] == result[:2], (native,target,facing,result,list(motion))
        rows.append(dict(input=[*native,*target,bits(2),bits(facing),bits(turn),bits(0.25),delay],output=result))
    assert calls == dict.fromkeys(calls,len(rows)), calls
    sequence = hashlib.sha256(json.dumps(rows,separators=(',',':')).encode()).hexdigest()
    report = dict(passed=True,binary_sha256=TARGET,cases=len(rows),engine_exact_cases=len(rows) if engine else 0,
                  calls=calls,startup=startup,word_sequence_sha256=sequence,scope=__doc__)
    if args.fixture:
        args.fixture.write_text(json.dumps(dict(version=1,binary_sha256=TARGET,scope=__doc__,
            source_entries=['6f16fbd0','6f165ae0','6f16f630','6f170880'],rows=rows),separators=(',',':'))+'\n')
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
