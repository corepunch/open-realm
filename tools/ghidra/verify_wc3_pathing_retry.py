#!/usr/bin/env python3
"""Original complete retry initialization and consumption; no replaced routines.

Complete native-source distance, registered mover/group lookup and owner PRNG
calls cover initialization and null-target terminal consumption. Historical
post-distance selection slices remain explicit controls. Target perimeter,
group-membership producers and full crowd scheduling remain separate.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import struct
from pathlib import Path
from verify_wc3_pathing_numeric import bits, initialize_runtime_scalars


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--fixture', type=Path)
    args = parser.parse_args()
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

    machine.mem_map(0x10000000, 0x20000)
    machine.mem_map(0x20000000, 0x10000)
    system, output = 0x10000000, 0x10000800
    stack, stop = 0x20008000, 0x30000000

    def write(address, *values):
        machine.mem_write(address, struct.pack('<' + 'I' * len(values), *(v & 0xffffffff for v in values)))

    def read(address, count=1):
        return list(struct.unpack('<' + 'I' * count, machine.mem_read(address, count * 4)))

    def run(entry, self, *arguments):
        write(stack, stop, *arguments)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, self)
        machine.emu_start(entry, stop, count=10000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail target lookup exceeded instruction budget')

    frame, group, owner = stack - 0x1000, system + 0x1000, system + 0x2000
    write(0x6fd53a48, owner)
    machine.mem_write(0x6fd54190, struct.pack('<f', 144.0))
    selection_cases, selected = 0, set()
    for distance_sq, members, seed in itertools.product(
            [0, 1, 143.99998474121094, 144, 144.00001525878906, 145, 10000],
            [0, 1, 2, 10], [0, 1, 0x12345678, 0xffffffff]):
        write(owner, seed, 0)
        write(group + 0x38, members)
        write(frame, 0, stop)
        machine.mem_write(frame + 8, struct.pack('<f', distance_sq))
        machine.reg_write(UC_X86_REG_EBP, frame)
        machine.reg_write(UC_X86_REG_ESP, frame - 0x10)
        machine.reg_write(UC_X86_REG_ESI, system)
        machine.emu_start(0x6f1689e8, 0x6f1689ff, count=100)
        assert machine.reg_read(UC_X86_REG_EIP) == 0x6f1689ff
        machine.reg_write(UC_X86_REG_EAX, group)
        machine.emu_start(0x6f168a04, stop, count=1000)
        assert machine.reg_read(UC_X86_REG_EIP) == stop
        result = read(system + 0x98)[0]
        short = distance_sq <= 144 and members <= 1
        assert result == 2 if short else result in (7, 8)
        assert (read(owner, 2) == [seed, 0]) == short
        selected.add(result)
        selection_cases += 1
    assert selected == {2, 7, 8}

    consume_cases = 0
    for initial in list(range(1, 33)) + [0x7fffffff, 0x80000000, 0xffffffff]:
        write(system + 0xa4, 0)
        write(system + 0x50, 0)
        write(system + 0x70, 3, 5, 2)
        write(system + 0x88, 0x30100000)
        write(system + 0x98, initial)
        run(0x6f167290, system, output)
        terminal = initial == 1
        assert machine.reg_read(UC_X86_REG_EAX) == (4 if terminal else 1)
        assert read(system + 0x98)[0] == (initial if terminal else initial - 1)
        assert read(system + 0x74)[0] == (5 if terminal else 0xffffffff)
        assert read(system + 0x70)[0] == 3 and read(system + 0x78)[0] == 2
        assert read(system + 0x88)[0] == (0x30100000 if terminal else 0x30000000)
        consume_cases += 1
    sequences = {}
    for initial in (2, 7, 8):
        write(system + 0xa4, 0)
        write(system + 0x98, initial)
        values = []
        for _ in range(initial + 1):
            run(0x6f167290, system, output)
            values.append([machine.reg_read(UC_X86_REG_EAX), read(system + 0x98)[0]])
        assert values == [[1, n] for n in range(initial - 1, 0, -1)] + [[4, 1], [4, 1]]
        sequences[initial] = values

    # Complete initialization and terminal consumption with actual registered
    # mover/group resolution, original native distance arithmetic and owner draws.
    startup=initialize_runtime_scalars(machine,stack,stop)
    registry,slots,mover,point,storage=[system+n for n in (0x3000,0x3100,0x4000,0x5000,0x6000)]
    write(0x6fd68610,registry);write(registry+0xc,slots);write(registry+0x1c,2);write(registry+0x3c,0)
    for index,obj in enumerate([mover,group]):
        write(slots+index*8,-2,obj);write(obj+0x14,index,100+index)
    write(mover+0x9c,1,101);write(mover+0xa8,system);write(0x6fd53a8c,mover)
    engine=ctypes.CDLL(str(args.engine.resolve())) if args.engine else None
    if engine:
        engine.pathing_retry_init.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)]
        engine.pathing_retry_advance.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)]
    init_rows=[];advance_rows=[]
    origins=[(0,0),(struct.unpack('<f',struct.pack('<I',0x3fb7b01a))[0],1.789),(8.125001,8.49999)]
    deltas=[(0,0),(11.999999046,0),(12,0),(12.00000095,0),(8,8),(16,0),(-8,-8)]
    for origin,delta,members,seed in itertools.product(origins,deltas,[0,1,2,10],[0,1,0x12345678,0xffffffff]):
        native=[bits(v) for v in origin];goal=[bits(origin[k]+delta[k]) for k in range(2)]
        write(point,*native);write(system+0x24,*goal);write(group+0x38,members);write(owner,seed,0)
        write(system+0x98,0)
        run(0x6f1689d0,system,point)
        output_words=[read(system+0x98)[0],*read(owner,2)]
        assert output_words[0] in (2,7,8)
        assert (output_words[1:]==[seed,0])==(output_words[0]==2)
        supplied=[*native,*goal,members,seed,0]
        if engine:
            out=(ctypes.c_uint32*3)();engine.pathing_retry_init((ctypes.c_uint32*7)(*supplied),out)
            assert list(out)==output_words,(supplied,list(out),output_words)
        init_rows.append(dict(input=supplied,output=output_words))
        for initial in [0,1,2,7,8,0xffffffff]:
            write(system+0x40,storage);write(storage,*goal,*native,*goal)
            write(system+0x50,3);write(system+0x70,5,2,4)
            write(system+0x88,0x30100000);write(system+0x94,7,initial);write(system+0xa4,0)
            write(owner,seed,0)
            run(0x6f167290,system,point)
            from unicorn.x86_const import UC_X86_REG_EAX
            output_words=[machine.reg_read(UC_X86_REG_EAX),read(system+0x98)[0],
                          read(system+0x50)[0],*read(system+0x74,2),read(system+0x70)[0],
                          read(system+0x88)[0],read(system+0x94)[0],*read(owner,2)]
            assert output_words[0]==(4 if initial==1 else 1)
            assert output_words[2:8]==([3,2,4,5,0x30100000,7] if initial==1 else
                                        [0,0xffffffff,4,5,0x30000000,7])
            supplied=[initial,*native,*goal,members,seed,0]
            if engine:
                out=(ctypes.c_uint32*4)();engine.pathing_retry_advance((ctypes.c_uint32*8)(*supplied),out)
                assert list(out)==[output_words[0],output_words[1],*output_words[-2:]]
            advance_rows.append(dict(input=supplied,output=output_words))
    sequence=hashlib.sha256(json.dumps([init_rows,advance_rows],separators=(',',':')).encode()).hexdigest()
    if args.fixture:
        args.fixture.write_text(json.dumps(dict(version=1,binary_sha256=digest,
            scope='Complete original retry init/advance with null target, registered groups and supplied native sources; no substituted routines.',
            source_entries=['6f1689d0','6f167290'],initializations=init_rows,advances=advance_rows),separators=(',',':'))+'\n')
    report = dict(binary_sha256=digest, scope=__doc__, passed=True,
                  complete_init_cases=len(init_rows),complete_advance_cases=len(advance_rows),startup=startup,
                  engine_exact_init_cases=len(init_rows) if engine else 0,
                  engine_exact_advance_cases=len(advance_rows) if engine else 0,
                  complete_word_sequence_sha256=sequence,
                  selection_cases=selection_cases, observed_initial_counts=sorted(selected), consume_cases=consume_cases, sequences=sequences)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
