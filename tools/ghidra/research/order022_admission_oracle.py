#!/usr/bin/env python3
"""Run original admission/owner branches with explicit callee-boundary observations.

This isolates control flow; interception and container callees are instrumented
boundaries, not a replacement implementation of retail tasks or callbacks. Live
captures separately establish real Repair interception and owner Stop dispatch.
"""
import argparse
import itertools
import json
from pathlib import Path
from supportgoal_ramp_oracle import OriginalRamp


def original(binary):
    from unicorn import UC_HOOK_CODE
    e = OriginalRamp(binary)
    x, u = e.X, e.u
    unit, order, head = e.terrain, e.output, e.vertices
    events = []
    intercept = False
    functions = {0x6f69b2f0: ('intercept', 1), 0x6f673e80: ('cancel-successors', 0),
                 0x6f673610: ('cancel-all', 0), 0x6f693490: ('append', 1),
                 0x6f691c70: ('insert', 3), 0x6f673fe0: ('cancel-internal', 0),
                 0x6f061320: ('resolve-head', 0), 0x6f694a10: ('pop-head', 0),
                 0x6f690930: ('create-immediate', 1)}

    def read(address, count=1):
        import struct
        return list(struct.unpack('<' + 'I' * count, u.mem_read(address, 4 * count)))

    def observe(machine, address, size, data):
        if address not in functions:
            return
        name, count = functions[address]
        sp = u.reg_read(x.UC_X86_REG_ESP)
        args = read(sp + 4, count)
        self = u.reg_read(x.UC_X86_REG_ECX)
        result = 1 if name == 'intercept' and intercept else head if name == 'resolve-head' else order if name == 'create-immediate' else 0
        event = dict(name=name, args=args)
        if name == 'create-immediate':
            event.update(command=self, owner=u.reg_read(x.UC_X86_REG_EDX))
        else:
            if name != 'resolve-head' and self != unit:
                raise ValueError('callee receiver ABI differs')
        events.append(event)
        u.reg_write(x.UC_X86_REG_EAX, result)
        u.reg_write(x.UC_X86_REG_ESP, sp + 4 + 4 * count)
        u.reg_write(x.UC_X86_REG_EIP, read(sp)[0])

    u.hook_add(UC_HOOK_CODE, observe)
    cases = []
    for dead, depth, mode, intercept, present, drop in itertools.product((0, 0x100), (-1, 0, 1, 2), (0, 1), (False, True), (False, True), (0, 8)):
        e.write(unit + 0x5c, dead)
        e.write(unit + 0x198, depth)
        e.write(unit + 0x19c, *([1, 2] if present else [0xffffffff, 0xffffffff]))
        e.write(head + 0x20, drop)
        events.clear()
        e.call(0x6f680320, [order, mode, 1])
        cases.append(dict(dead=dead, depth=depth, mode=mode, intercept=intercept,
                          present=present, drop=drop, events=list(events)))
    owners = []
    for widget, dead, owner in itertools.product((0, 1), (0, 0x100), (0, 3, 15)):
        e.write(unit + 0x20, widget)
        e.write(unit + 0x58, owner)
        e.write(unit + 0x5c, dead)
        e.write(unit + 0x198, 0)
        intercept = False
        events.clear()
        e.write(e.stack, e.stop)
        u.reg_write(x.UC_X86_REG_ESP, e.stack)
        u.reg_write(x.UC_X86_REG_EBX, unit)
        u.emu_start(0x6f698d68, 0x6f698d9b, count=10000)
        if u.reg_read(x.UC_X86_REG_EIP) != 0x6f698d9b or u.reg_read(x.UC_X86_REG_ESP) != e.stack:
            raise ValueError('owner admission slice ABI/budget differs')
        owners.append(dict(widget=widget, dead=dead, owner=owner, events=list(events)))
    return dict(admission=cases, owner=owners,
                limits='Callee boundaries observed, not executed; excludes task/container behavior.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        parser.error('fresh report required')
    result = original(args.binary)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print('original admission cases', len(result['admission']), 'owner slices', len(result['owner']))
