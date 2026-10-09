#!/usr/bin/env python3
"""Execute retail heap ordering, fine-node reopening, and loop termination.

Preallocated storage keeps retail container growth below capacity. Synthetic
node lists isolate relaxation states. Loop tests stub neighbour expansion;
this is not a full map-search oracle.
"""
import argparse
import hashlib
import itertools
import json
import random
import struct
from pathlib import Path


def main():
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_ECX, UC_X86_REG_EAX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
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
    system, nodes, storage, output = 0x10000000, 0x10001000, 0x10002000, 0x10000800
    heap, stack, stop = system + 0x44, 0x20008000, 0x30000000

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
            raise RuntimeError('retail queue operation exceeded instruction budget')

    def reset_heap():
        write(heap + 0xc, storage)
        write(heap + 0x18, 4096, 1)

    def contents():
        count = read(heap + 0x1c)[0]
        return [tuple(read(storage + 12 * i, 3)) for i in range(1, count)]

    def push(model, record):
        model.append(record)
        index = len(model) - 1
        while index > 1 and model[index // 2][0] >= record[0]:
            model[index] = model[index // 2]
            index //= 2
        model[index] = record

    def pop(model):
        first, last = model[1], model.pop()
        if len(model) > 1:
            index = 1
            while index * 2 < len(model):
                child = index * 2
                if child + 1 < len(model) and model[child + 1][0] <= model[child][0]:
                    child += 1
                if last[0] <= model[child][0]:
                    break
                model[index] = model[child]
                index = child
            model[index] = last
        return first

    sequences = itertools.chain.from_iterable(itertools.product(range(3), repeat=n) for n in range(1, 9))
    rng = random.Random(12717085)
    extra = [tuple(rng.choice((0, 1, 0x7fffffff, 0x80000000, 0xffffffff)) for _ in range(64)) for _ in range(256)]
    heap_cases, operations = 0, 0
    equal_example = []
    for keys in itertools.chain(sequences, extra):
        reset_heap()
        model = [None]
        order = []
        for index, key in enumerate(keys):
            record = (key, index, index * 3 + 1)
            run(0x6f1483f0, heap, *record)
            push(model, record)
            if contents() != model[1:]:
                raise RuntimeError('heap insertion or tie-order mismatch')
            operations += 1
        while len(model) > 1:
            run(0x6f148240, heap, output)
            actual = tuple(read(output, 3))
            if actual != pop(model) or contents() != model[1:]:
                raise RuntimeError('heap removal or tie-order mismatch')
            order.append(actual[1])
            operations += 1
        if keys == (0,) * 8:
            equal_example = order
        heap_cases += 1

    relaxation_cases, records = 0, []
    for state, old_cost in itertools.product(('fresh', 'open', 'closed_only', 'closed_head', 'closed_tail', 'closed_middle'), (120, 121, 122)):
        machine.mem_write(nodes, bytes(0x100))
        reset_heap()
        write(system + 0x30, nodes)
        write(system + 0x64, -3)
        write(system + 0x88, 0, 0)
        write(system + 0x98, -1, -1)
        write(nodes, 2, 5, 10, -1, -1, old_cost, 83, 9)
        write(nodes + 3 * 0x24 + 0x14, 100)
        if state == 'open':
            write(nodes + 0xc, -2, -1)
            run(0x6f1483f0, heap, old_cost + 83, 0, 10)
        elif state.startswith('closed'):
            write(nodes + 0xc, -3, -1)
            write(system + 0x64, 0)
            if state == 'closed_head':
                write(nodes + 0xc, 1, -1)
                write(nodes + 0x24 + 0xc, -3, 0)
            elif state in ('closed_tail', 'closed_middle'):
                write(system + 0x64, 1)
                write(nodes + 0xc, -3 if state == 'closed_tail' else 2, 1)
                write(nodes + 0x24 + 0xc, 0, -1)
                if state == 'closed_middle':
                    write(nodes + 2 * 0x24 + 0xc, -3, 0)
        before = read(nodes, 27)
        head_before = read(system + 0x64)[0]
        queue_before = contents()
        run(0x6f14a560, system, 0, 3, 21)
        accepted = state == 'fresh' or old_cost > 121
        generation = 11 if state == 'fresh' else 12
        if not accepted:
            if read(nodes, 27) != before or contents() != queue_before or read(system + 0x64)[0] != head_before:
                raise RuntimeError('equal/worse cost changed a discovered node')
        else:
            if read(nodes + 8, 6) != [generation, 0xfffffffe, 0xffffffff, 121, 83, 3]:
                raise RuntimeError('cheaper path failed to reopen/requeue node')
            if sorted(contents()) != sorted(queue_before + [(204, 0, generation)]):
                raise RuntimeError('generation-tagged queue replacement mismatch')
            if state.startswith('closed'):
                expected_head = -3 if state == 'closed_only' else 1
                if read(system + 0x64)[0] != (expected_head & 0xffffffff):
                    raise RuntimeError('closed list head not repaired')
                if state == 'closed_head' and read(nodes + 0x24 + 0x10)[0] != 0xffffffff:
                    raise RuntimeError('closed successor predecessor not repaired')
                if state in ('closed_tail', 'closed_middle'):
                    if read(nodes + 0x24 + 0xc)[0] != (0xffffffff - 2 if state == 'closed_tail' else 2):
                        raise RuntimeError('closed predecessor successor not repaired')
                    if state == 'closed_middle' and read(nodes + 2 * 0x24 + 0x10)[0] != 1:
                        raise RuntimeError('closed successor predecessor not repaired')
        records.append(dict(state=state, old_cost=old_cost, accepted=accepted,
                            generation=read(nodes + 8)[0], heap=contents()))
        relaxation_cases += 1
    # Isolate the real pop/stale/goal/budget/closed-list loop. Expansion emits
    # no neighbours, optionally setting the observed special-object flag.
    special = False
    expanded = []

    def expand(uc, address, size, data):
        sp = uc.reg_read(UC_X86_REG_ESP)
        ret, index = read(sp, 2)
        expanded.append(index)
        if special:
            write(system + 0xcc, 1)
        uc.reg_write(UC_X86_REG_ESP, sp + 8)
        uc.reg_write(UC_X86_REG_EIP, ret)

    machine.hook_add(UC_HOOK_CODE, expand, begin=0x6f14b760, end=0x6f14b760)
    loop_cases = 0
    patterns = itertools.chain.from_iterable(itertools.product(range(4), repeat=n) for n in range(5))
    for pattern in patterns:
        for budget, special in itertools.product(range(5), (False, True)):
            reset_heap()
            write(system + 0x30, nodes)
            write(system + 0x64, -3, budget, 0)
            write(system + 0x94, 1)
            write(system + 0xcc, 0)
            for index in range(2):
                write(nodes + index * 0x24, index, 0, 10, -2, -1, 0, 0, -1)
            model = [None]
            for position, value in enumerate(pattern):
                record = (position % 3, value // 2, 9 + value % 2)
                run(0x6f1483f0, heap, *record)
                push(model, record)
            gens, links, head, pops, result, expected_expanded = [10, 10], [0xfffffffe] * 2, 0xfffffffd, 0, 0xffffffff, []
            while len(model) > 1:
                old_pops, pops = pops, pops + 1
                if budget <= old_pops:
                    break
                key, index, generation = pop(model)
                if gens[index] != generation:
                    continue
                gens[index] += 1
                links[index] = 0xffffffff
                if index == 1:
                    result = index
                    break
                expected_expanded.append(index)
                if special:
                    result = index
                    break
                gens[index] += 1
                links[index], head = head, index
            expanded.clear()
            run(0x6f14a4c0, system)
            actual = machine.reg_read(UC_X86_REG_EAX)
            if (actual != result or read(system + 0x6c)[0] != pops or contents() != model[1:] or
                read(system + 0x64)[0] != head or expanded != expected_expanded or
                [read(nodes + i * 0x24 + 8)[0] for i in range(2)] != gens or
                [read(nodes + i * 0x24 + 0xc)[0] for i in range(2)] != links or read(system + 0xcc)[0] != 0):
                raise RuntimeError(f'search-loop mismatch pattern={pattern} budget={budget} special={special}')
            loop_cases += 1

    report = dict(binary_sha256=digest, heap_sequences=heap_cases, heap_operations=operations,
                  relaxation_cases=relaxation_cases, loop_cases=loop_cases, mismatches=[], equal_key_eight_ids=equal_example,
                  scope='original heap/container/relaxation/list; loop cases stub only expansion to no-neighbours/optional special flag; preallocated storage; no complete map search',
                  relaxation=records)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'{heap_cases} heap sequences / {operations} operations; {relaxation_cases} relaxation / {loop_cases} loop cases; all pass')


if __name__ == '__main__':
    main()
