#!/usr/bin/env python3
"""Verify retail scheduler initialization, FIFO admission and interval gates.

Original x86 executes without stubs; synthetic requests isolate scheduling.
No movement or simulation timing is emulated. Work charging executes only the
original post-search instruction slices, with explicit synthetic boundary inputs.
"""
import argparse
import hashlib
import itertools
import json
import random
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--header', type=Path, help='Emit row-zero original-code cases for the engine regression')
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
            raise RuntimeError('retail scheduler operation exceeded instruction budget')

    table = 0x6fd53a90
    # Execute the complete initializer up to (excluding) CRT destructor registration.
    machine.reg_write(UC_X86_REG_ESP, stack)
    machine.emu_start(0x6f003d20, 0x6f0040c0, count=10000)
    if machine.reg_read(UC_X86_REG_EIP) != 0x6f0040c0:
        raise RuntimeError('initializer did not reach expected boundary')
    defaults = [(5000, 3, 800), (2000, 2, 300), (400, 2, 900), (700, 1, 1100)]
    for row in range(16):
        for kind, (limit, reload, budget) in enumerate(defaults):
            assert read(table + row * 0x70 + kind * 0x1c, 7) == [limit | reload << 16, budget, 0, 0, 0, 0, 0]

    selection_cases = 0
    for row, flags, limit in itertools.product(range(16), range(4), [0, 399, 400, 401, 2000, 5000, 65535]):
        write(system + 0x84, limit << 16)
        write(system + 0x88, row << 16 | flags << 25)
        run(0x6f1679c0, system)
        kind = 3 if flags & 1 else 2 if limit <= 400 else 1 if flags & 2 else 0
        assert machine.reg_read(UC_X86_REG_EAX) == table + row * 0x70 + kind * 0x1c
        selection_cases += 1

    # Distinct values per bucket prove that the updater visits every bucket.
    cadence_checks = 0
    model = []
    for index in range(64):
        bucket = table + index * 0x1c
        reload = defaults[index % 4][1]
        write(bucket + 8, 1000 + index, index % 5)
        model.append([1000 + index, index % 5, reload])
    for tick in range(20):
        run(0x6f167310, 0)
        for index, state in enumerate(model):
            state[0:2] = [0, state[2]] if state[1] == 0 else [state[0], state[1] - 1]
            assert read(table + index * 0x1c + 8, 2) == state[:2]
            cadence_checks += 1

    # Each trial starts with a valid empty intrusive list. Exercise arbitrary
    # removals, repeat requests, equality/unsigned limits, and head blocking.
    rng = random.Random(12717085)
    paths = [system + 0x100 * (i + 1) for i in range(8)]
    operations = 0
    bucket = table
    for trial in range(128):
        write(bucket + 4, 300, 0, 0, 0, 0, 0)
        for path in paths:
            write(path + 0x8c, 0, 0)
        queue = []
        for step in range(128):
            path = rng.choice(paths)
            work = rng.choice([0, 299, 300, 301, 0x80000000, 0xffffffff])
            write(bucket + 8, work)
            if rng.randrange(4) == 0:
                run(0x6f1686a0, bucket, path)
                if path in queue:
                    queue.remove(path)
            else:
                expected = work <= 300 and (not queue or queue[0] == path)
                run(0x6f168310, bucket, path)
                assert machine.reg_read(UC_X86_REG_EAX) == int(expected)
                if expected and path in queue:
                    queue.remove(path)
                elif not expected and path not in queue:
                    queue.append(path)
            assert read(bucket + 0x10, 3) == [len(queue), queue[0] if queue else 0, queue[-1] if queue else 0]
            for item in paths:
                if item not in queue:
                    expected_links = [0, 0]
                else:
                    i = queue.index(item)
                    expected_links = [queue[i-1] if i else 0xffffffff, queue[i+1] if i+1 < len(queue) else 0xffffffff]
                assert read(item + 0x8c, 2) == expected_links
            operations += 1

    interval_cases = 0
    values = [0, 1, 9, 10, 11, 100, 0x7fffffff, 0xfffffff5, 0xffffffff]
    assert read(0x6fa91c14, 2) == [10, 10]
    for mode, timestamp, current in itertools.product(range(2), values, values):
        write(system + 0x7c, 0x12345678, 0x23456789)
        write(system + 0x7c + 4 * mode, timestamp)
        adjusted = (current - 10) & 0xffffffff if current < timestamp else timestamp
        elapsed = (current - adjusted) & 0xffffffff
        accepted = elapsed >= 10
        run(0x6f168910, system, mode, current, output)
        assert machine.reg_read(UC_X86_REG_EAX) == int(accepted)
        assert read(output)[0] == elapsed
        assert read(system + 0x7c + 4 * mode)[0] == (current if accepted else adjusted)
        assert read(system + 0x7c + 4 * (1-mode))[0] == [0x12345678, 0x23456789][1-mode]
        interval_cases += 1

    # Reassignment must unlink using the old row before changing the row bits.
    transition_cases = 0
    for old_row, new_row, kind in itertools.product(range(16), range(16), range(4)):
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.emu_start(0x6f003d20, 0x6f0040c0, count=10000)
        old_bucket = table + old_row * 0x70 + kind * 0x1c
        path = paths[1]
        flags = (old_row << 16) | 0x200000 | (0x2000000 if kind == 3 else 0x4000000 if kind == 1 else 0)
        write(path + 0x84, (400 if kind == 2 else 5000) << 16 | 700)
        write(path + 0x88, flags)
        for item in paths[:3]:
            write(item + 0x8c, 0, 0)
            run(0x6f165ea0, old_bucket, item)
        run(0x6f168a80, path, new_row)
        assert read(path + 0x88)[0] == ((flags & ~0x20f0000) | new_row << 16)
        assert read(path + 0x8c, 2) == [0, 0]
        assert read(old_bucket + 0x10, 3) == [2, paths[0], paths[2]]
        assert read(paths[0] + 0x90)[0] == paths[2]
        assert read(paths[2] + 0x8c)[0] == paths[0]
        for row in range(16):
            if row != old_row:
                for k in range(4):
                    assert read(table + row * 0x70 + k * 0x1c + 0x10, 3) == [0, 0, 0]
        transition_cases += 1

    target_transition_cases = 0
    for row, fine, old_target, new_target, small in itertools.product(range(16), range(2), range(2), range(2), range(2)):
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.emu_start(0x6f003d20, 0x6f0040c0, count=10000)
        kind = 3 if fine else 2 if small else 1 if old_target else 0
        bucket = table + row * 0x70 + kind * 0x1c
        path = paths[0]
        flags = (row << 16) | fine << 25 | old_target << 26 | 0x200000
        write(path + 0x84, (400 if small else 5000) << 16 | 700)
        write(path + 0x88, flags, 0, 0)
        run(0x6f165ea0, bucket, path)
        run(0x6f168ab0, path, new_target)
        changed = old_target != new_target
        expected_flags = ((flags & ~0x6000000) | new_target << 26) if changed else flags
        assert read(path + 0x88)[0] == expected_flags
        assert read(path + 0x8c, 2) == ([0, 0] if changed else [0xffffffff, 0xffffffff])
        assert read(bucket + 0x10, 3) == ([0, 0, 0] if changed else [1, path, path])
        assert read(path + 0x84)[0] == ((400 if small else 5000) << 16 | 700)
        target_transition_cases += 1

    # Execute the actual ADD/CMP/conditional timestamp stores. These boundary
    # states are synthetic: admitted stock work plus a bounded search cannot
    # reach UINT32_MAX. Compare admission after a wrap with a clean counter.
    wrap_cases=[]
    for row,kind,old,charge in itertools.product(range(16),range(4),
            [0,63,1100,0xffffffe0,0xffffffff],[0,1,31,32,63,64,701,5001]):
        bucket=table+row*0x70+kind*0x1c
        path=paths[0]
        write(bucket+4,defaults[kind][2],old,0,0,0,0)
        write(path+0x7c,1037,1037)
        machine.reg_write(UC_X86_REG_EAX,charge)
        machine.reg_write(UC_X86_REG_EBX,path)
        machine.reg_write(UC_X86_REG_ECX,bucket)
        machine.reg_write(UC_X86_REG_ESI,bucket)
        entry,end=(0x6f166fcd,0x6f166fdd) if kind==3 else (0x6f166db3,0x6f166dc5)
        machine.emu_start(entry,end,count=10)
        if machine.reg_read(UC_X86_REG_EIP)!=end:raise RuntimeError('charge slice did not finish')
        work=(old+charge)&0xffffffff
        time=0 if (work<64 if kind==3 else charge<32) else 1037
        assert read(bucket+8)[0]==work
        assert read(path+0x7c+(0 if kind==3 else 4))[0]==time
        write(path+0x8c,0,0)
        run(0x6f168310,bucket,path)
        actual=machine.reg_read(UC_X86_REG_EAX)
        links=read(path+0x8c,2);fifo=read(bucket+0x10,3)
        write(bucket+8,work,0,0,0,0)
        write(path+0x8c,0,0)
        run(0x6f168310,bucket,path)
        assert machine.reg_read(UC_X86_REG_EAX)==actual==int(work<=defaults[kind][2])
        assert read(path+0x8c,2)==links and read(bucket+0x10,3)==fifo
        wrap_cases.append([row,kind,old,charge,work,time,actual])

    report = dict(binary_sha256=digest, scope=__doc__, passed=True,
                  initialized_buckets=64, defaults=defaults,
                  class_transition_cases=transition_cases,
                  target_transition_cases=target_transition_cases,
                  selection_cases=selection_cases, cadence_checks=cadence_checks,
                  queue_operations=operations, interval_cases=interval_cases,
                  work_charge_cases=len(wrap_cases), work_charge_rows=wrap_cases,
                  work_charge_slices=[[0x6f166fcd,0x6f166fdd],[0x6f166db3,0x6f166dc5]])
    if args.header:
        header=['/* Generated from original x86 post-search charge slices; boundary states are synthetic. */',
                '/* SHA256 '+digest+' */','static uint32_t const retail_scheduler_work[][6]={']
        for row in wrap_cases:
            if row[0]==0:header.append('    {'+','.join(str(v)+'u' for v in row[1:])+'},')
        args.header.write_text('\n'.join(header+['};','']))
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
