#!/usr/bin/env python3
"""ACC-02.2 research: dynamic writer census for adaptive cell storage.

Runs original producers and complete requests (same inputs as ACC-01.2 structured
scenarios, all four lanes, sizes 1/2, warp 0/1, plus 15d360 mode-1 exclusion and
mode-0 restore rectangles) with a passive UC_HOOK_MEM_WRITE over the four adaptive
cell arrays. Records every (writer PC, byte offset within the 8-byte cell, width)
and, for class-byte writes, every resulting lane pair value. Expected writers:
15c030 (+6 byte), 15cf80/15d470 (+7 byte := 0), 15d0e0/15d1c0 (+4 dword RMW of
lane bits), 163ef0 (+0 stamp, +4 ushort index). Any class pair 11 fails the run.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_acc01_2_witnesses import Runner, scenarios  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--witness-maps', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    from unicorn import UC_HOOK_MEM_WRITE
    from unicorn.x86_const import UC_X86_REG_EIP
    runner = Runner(args.binary)
    r = runner.retail
    census, class3 = {}, []
    ranges = [(storage, storage + side * side * 8, level) for level, (storage, side) in enumerate(zip(r.data, r.sides))]

    def write(uc, access, address, size, value, user):
        for lo, hi, level in ranges:
            if lo <= address < hi:
                pc = uc.reg_read(UC_X86_REG_EIP)
                offset = (address - lo) % 8
                key = f'{pc:08x}:+{offset}:w{size}:level{level}'
                census[key] = census.get(key, 0) + 1
                if offset <= 7 < offset + size:
                    byte7 = (value >> (8 * (7 - offset))) & 0xff
                    if any(((byte7 >> shift) & 3) == 3 for shift in (0, 2, 4, 6)):
                        class3.append(dict(pc=f'{pc:08x}', level=level, value=f'{value:x}'))
                return
    for lo, hi, level in ranges:
        r.uc.hook_add(UC_HOOK_MEM_WRITE, write, begin=lo, end=hi - 1)
    requests = 0
    for number, scenario in enumerate(scenarios(args.witness_maps)):
        if number % 20 == 0:
            print(number, scenario['name'], requests, flush=True)
        for lane in (0, 2, 4, 6):
            runner.build(scenario, lane)
            for source, goal, size, budget, warp in scenario['requests']:
                r.search(lane, source, goal, budget=budget, size_input=size, warp=warp)
                requests += 1
            # exclusion scope around the source and restore (15d360 mode1 then mode0)
            rect = (8, 8, 24, 24)
            r.rebuild(rect, 1)
            r.search(lane, (4.25, 4.75), (27.25, 27.75), budget=400, size_input=0, warp=1)
            r.rebuild(rect, 0)
            requests += 1
    if class3:
        raise SystemExit(f'class pair 11 written: {class3[:4]}')
    report = dict(binary_sha256=r.digest, requests=requests, writers=dict(sorted(census.items())),
                  writer_pcs=sorted({k.split(':')[0] for k in census}), class_pair_11_writes=0,
                  scope='passive write census of the four adaptive cell arrays during producer builds, gate publications, exclusions and complete requests')
    args.out.write_text(json.dumps(report, indent=1) + '\n')
    print(requests, 'requests;', 'writer PCs', report['writer_pcs'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
