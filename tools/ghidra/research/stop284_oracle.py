#!/usr/bin/env python3
"""Execute original Stop task production; substitute only the two task factories.

The supplied factories record their arguments and return without allocating.
This proves production order and ABI, not allocation or dispatch behaviour.
Those lifetimes are observed separately in the complete retail captures.
"""
import argparse, json
from pathlib import Path
from foot03_spatial_harness_copy import Emu


def original(binary):
    from unicorn import UC_HOOK_CODE
    e = Emu(binary)
    unit = e.fixture(0x200)
    calls = []

    def factory(uc, address, size, data):
        sp = uc.reg_read(e.X.UC_X86_REG_ESP)
        if uc.reg_read(e.X.UC_X86_REG_ECX) != unit:
            raise ValueError('Stop factory owner differs')
        calls.append(dict(kind='event' if address == 0x6f691f20 else 'action', value=e.r(sp + 4)))
        uc.reg_write(e.X.UC_X86_REG_EIP, e.r(sp))
        uc.reg_write(e.X.UC_X86_REG_ESP, sp + 8)

    for address in (0x6f691f20, 0x6f6927a0):
        e.uc.hook_add(UC_HOOK_CODE, factory, begin=address, end=address)
    e.call(0x6f690b80, unit)
    if e.esp_after != 4:
        raise ValueError('Stop producer ABI differs')
    return dict(created=calls, execution=list(reversed(calls)), stack_bytes=e.esp_after)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    if a.report.exists():
        p.error('fresh report required')
    a.report.write_text(json.dumps(original(a.binary), indent=2) + '\n')
