#!/usr/bin/env python3
"""Original moving-blocker yielding, including actual handle resolution.

Synthetic registered movers/groups, real arithmetic and identity/countdown
writers. No stubs; contains no retail executable bytes.
"""
import argparse
import ctypes
import hashlib
import itertools
import json
import math
import random
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EBP
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--engine', type=Path, help='compare the production ordered-decision and countdown kernels')
    parser.add_argument('--fixture', type=Path, help='freeze complete original decision/gate input and output words')
    args = parser.parse_args()
    engine=ctypes.CDLL(str(args.engine.resolve())) if args.engine else None
    if engine:
        engine.pathing_yield_decision.argtypes=[ctypes.POINTER(ctypes.c_uint32)]
        engine.pathing_yield_decision.restype=ctypes.c_uint32
        engine.pathing_yield_advance.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.c_uint32]
        engine.pathing_yield_advance.restype=ctypes.c_uint32
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
        machine.emu_start(entry, stop, count=100000)
        if machine.reg_read(UC_X86_REG_EIP) != stop:
            raise RuntimeError('retail target lookup exceeded instruction budget')

    path, other_path, mover, other, group_a, group_b, registry, slots, candidates, entries = [
        system + n for n in (0x1000,0x1200,0x2000,0x2200,0x3000,0x3200,0x4000,0x4100,0x5000,0x5100)]
    write(0x6fd68610, registry)
    write(registry + 0xc, slots)
    write(registry + 0x1c, 4)
    write(registry + 0x3c, 0)
    for index, obj in enumerate([mover, other, group_a, group_b]):
        write(slots + index*8, -2, obj)
        write(obj + 0x14, index, 100+index)
    write(0x6fd53a8c, mover)
    machine.mem_write(0x6fd3c740, struct.pack('<3f', -1,0,1))
    write(mover + 0xa8, path)
    write(other + 0xa8, other_path)
    write(mover + 0x9c, 2, 102)
    write(candidates + 0xc, entries)
    write(candidates + 0x1c, 1)
    write(entries, other)
    cases, self_waits, other_waits, skipped = 0,0,0,0
    frozen_decisions=[]; frozen_gates=[]
    for speed, other_speed, group, group_flags, player, other_player, blocked, prior in itertools.product(
            [0,1,2,3], [0,1,2,3], ['same','different','none'], [0,8], [0,1,15], [0,1,15],
            [False,True], [0,3,4,19,25]):
        machine.mem_write(mover + 0x80, struct.pack('<2f', speed,0))
        machine.mem_write(other + 0x80, struct.pack('<2f', other_speed,0))
        write(other + 0x9c, 2 if group=='same' else 3 if group=='different' else -1,
              102 if group=='same' else 103)
        write(group_a + 0x80, group_flags)
        write(path + 0x88, player << 16)
        write(other_path + 0x88, other_player << 16)
        write(path + 0x94, prior)
        write(other_path + 0x94, prior)
        write(path + 0xa8, 1,101)  # must be cleared before scanning
        write(other_path + 0xa8, 0 if blocked else -1,100 if blocked else -1)
        eligible = group != 'none' and other_speed != 0 and not blocked
        own_wait = eligible and ((group=='same' and not (group_flags & 8)) or
                                speed <= other_speed or player != other_player)
        peer_wait = eligible and not own_wait
        if engine:
            words=lambda v:struct.unpack('<I',struct.pack('<f',v))[0]
            supplied=(ctypes.c_uint32*10)(words(speed),0,player,words(other_speed),0,other_player,group_flags,group!='none',group=='same',blocked)
            assert engine.pathing_yield_decision(supplied)==(1 if own_wait else 2 if peer_wait else 0)
        run(0x6f168360, path, candidates)
        assert read(path + 0xa8,2) == ([1,101] if own_wait else [0xffffffff]*2)
        assert read(path + 0x94)[0] == (max(prior,4) if own_wait else prior)
        assert read(other_path + 0xa8,2) == ([0,100] if peer_wait or blocked else [0xffffffff]*2)
        assert read(other_path + 0x94)[0] == (max(prior,20) if peer_wait else prior)
        words=lambda v:struct.unpack('<I',struct.pack('<f',v))[0]
        frozen_decisions.append(dict(input=[words(speed),0,player,words(other_speed),0,other_player,group_flags,group!='none',group=='same',blocked],
            decision=1 if read(path+0xa8,2)==[1,101] else 2 if peer_wait else 0,
            prior=prior,output=[read(path+0x94)[0],*read(path+0xa8,2),read(other_path+0x94)[0],*read(other_path+0xa8,2)]))
        cases += 1
        self_waits += own_wait
        other_waits += peer_wait
        skipped += not eligible
    third, third_path = system + 0x2400, system + 0x1400
    write(registry + 0x1c, 5)
    write(slots + 4*8, -2, third)
    write(third + 0x14, 4,104)
    write(third + 0x9c, 3,103)
    write(third + 0xa8, third_path)
    write(other + 0x9c, 3,103)
    machine.mem_write(mover + 0x80, struct.pack('<2f',3,0))
    patterns = [[other,third],[third,other],[other,other],[0,other],[0,third],[third,0,other],[],[other,third,other]]
    sequence_cases = 0
    for sequence, speed_a, speed_b in itertools.product(patterns,[0,1,3,4],[0,1,3,4]):
        write(candidates + 0x1c, len(sequence))
        if sequence:
            write(entries,*sequence)
        machine.mem_write(other + 0x80, struct.pack('<2f',speed_a,0))
        machine.mem_write(third + 0x80, struct.pack('<2f',speed_b,0))
        for p in [path,other_path,third_path]:
            write(p + 0x88, 0)
            write(p + 0x94, 0)
            write(p + 0xa8, -1,-1)
        waits = {other:False,third:False}
        own = None
        for candidate in sequence:
            if not candidate or waits[candidate]:
                continue
            speed = speed_a if candidate==other else speed_b
            if not speed:
                continue
            if speed >= 3:
                own = candidate
                break
            waits[candidate] = True
        run(0x6f168360,path,candidates)
        expected_id = [1,101] if own==other else [4,104] if own==third else [0xffffffff]*2
        assert read(path+0xa8,2)==expected_id
        assert read(path+0x94)[0]==(4 if own else 0)
        for candidate,p in [(other,other_path),(third,third_path)]:
            assert read(p+0xa8,2)==([0,100] if waits[candidate] else [0xffffffff]*2)
            assert read(p+0x94)[0]==(20 if waits[candidate] else 0)
        sequence_cases += 1

    point, destination = system + 0x9000, system + 0x9100
    gate_cases = 0
    for flags, countdown in itertools.product([0,0x100000,0xffffffff,0x12300000,0x12000000], [1,2,4,20,0xffffffff]):
        write(path + 0x88, flags)
        write(path + 0x94, countdown)
        write(path + 0xa8, 1,101)
        write(destination, 0x42a00000,0x42c80000)
        write(0x6fd53a80, 0xdeadbeef,0xdeadbeef,0xdeadbeef,0xdeadbeef)
        run(0x6f165ae0,path,point,destination,mover)
        disabled = bool(flags & 0x100000)
        assert machine.reg_read(UC_X86_REG_EAX) == (0x100000 if disabled else 1)
        assert read(path + 0x94)[0] == (countdown if disabled else countdown-1)
        if engine:
            value=ctypes.c_uint32(countdown)
            assert engine.pathing_yield_advance(ctypes.byref(value),disabled)==(0 if disabled else 1)
            assert value.value==read(path+0x94)[0]
        assert read(path + 0xa8,2) == [1,101]
        assert read(destination,2) == [0x42a00000,0x42c80000]
        assert read(0x6fd53a80,4) == [0xdeadbeef]*4
        frozen_gates.append(dict(flags=flags,delay=countdown,output=read(path+0x94)[0],result=machine.reg_read(UC_X86_REG_EAX)))
        gate_cases += 1
    delay_sequences = 0
    for initial in [4,20]:
        write(path + 0x88,0)
        write(path + 0x94,initial)
        for expected in range(initial-1,-1,-1):
            run(0x6f165ae0,path,point,destination,mover)
            assert read(path+0x94)[0] == expected
            assert machine.reg_read(UC_X86_REG_EAX) == 1
        # The following invocation proceeds to context setup; no routine replaced.
        write(stack,stop,point,destination,mover)
        machine.reg_write(UC_X86_REG_ESP,stack)
        machine.reg_write(UC_X86_REG_ECX,path)
        machine.emu_start(0x6f165ae0,0x6f1684d0,count=1000)
        assert machine.reg_read(UC_X86_REG_EIP)==0x6f1684d0
        delay_sequences += 1
    owner, fine, grid = system+0x6000,system+0x7000,system+0x8000
    write(0x6fd53a48,owner)
    write(owner+0x24c,fine)
    write(fine+0x1c,grid)
    setup_cases=0
    for radius in [0,0.499,0.5,0.999,1,1.499,1.5,2]:
        machine.mem_write(path+0xb4,struct.pack('<f',radius))
        run(0x6f1684d0,path,mover)
        cls=0 if radius<0.5 else 1 if radius<1 else 2 if radius<1.5 else 3
        assert struct.unpack('<H',machine.mem_read(0x6fd53a80,2))[0]==cls
        assert read(0x6fd53a84,3)==[fine,grid,mover]
        setup_cases+=1

    report = dict(binary_sha256=digest, scope=__doc__, passed=True, countdown_gate_cases=gate_cases, delay_sequences=delay_sequences, context_setup_cases=setup_cases,
                  decision_cases=cases, sequence_cases=sequence_cases,
                  engine_exact_decisions=cases if engine else 0, engine_exact_countdown_cases=gate_cases if engine else 0,
                  self_waits=self_waits, other_waits=other_waits, skipped_candidates=skipped)
    if args.fixture:
        args.fixture.write_text(json.dumps(dict(version=1,binary_sha256=digest,source_entries=['6f168360','6f165ae0'],
            decisions=frozen_decisions,gates=frozen_gates,scope='Complete original registered single-candidate decisions and enabled/disabled countdown gates. Engine integrates ordinary cohorts with default group flags0; group-bit8 producer, lazy cell-chain order, queued admission and full retail crowd trajectories remain open.'),separators=(',',':'))+'\n')
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
