#!/usr/bin/env python3
"""Export original instruction guards for the retreat473 Captain policy functions.

Read-only: queries the shared headless Ghidra /disassemble_function endpoint for instruction
addresses, then takes each instruction's bytes from the hash-checked retail game.dll. The
final instruction of each body is bounded by the function's next-instruction address from
Ghidra's listing order; it is omitted when that bound is not available.
"""
import argparse, hashlib, json, struct, urllib.request
from pathlib import Path

SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
FUNCTIONS = {
    '6f9c7b10': 'CaptainAI_InitAssault (Captain6c bit0)',
    '6f9cfeb0': 'CaptainAI_OnActorGoalEvent',
    '6f9d08e0': 'CaptainAI_ReevaluateHomeRetreat',
    '6f9d16c0': 'CaptainAI_UpdateMemberOrders (retreat member branch 9d17ef..9d185b)',
    '6f9d2670': 'CaptainAI_GoHome',
    '6f9d44d0': 'CaptainAI_PublishPointRequest',
    '6f9d7760': 'CaptainAI_OnMemberRemoved',
    '6f9d87d0': 'CaptainAI_ReissueMemberOrder',
}


def section_reader(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<I', data, pe + 24 + 28)[0]
    sections = [struct.unpack_from('<IIII', data, pe + 24 + opt + i * 40 + 8) for i in range(count)]

    def read(address, size):
        rva = address - base
        for vsize, vat, rsize, rat in sections:
            if vat <= rva < vat + min(vsize, rsize):
                return data[rat + rva - vat:rat + rva - vat + size]
        raise ValueError(hex(address))
    return read


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--ghidra', default='http://localhost:8089')
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    data = a.binary.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA:
        raise SystemExit('unsupported game.dll')
    read = section_reader(data)
    guards, bodies = {}, {}
    for address, name in FUNCTIONS.items():
        with urllib.request.urlopen(f'{a.ghidra}/disassemble_function?address=0x{address}') as r:
            listing = json.loads(r.read())['instructions']
        addrs = sorted(int(i['address'], 16) for i in listing)
        bodies[address] = dict(name=name, instructions=len(addrs) - 1)
        for cur, nxt in zip(addrs, addrs[1:]):
            if nxt - cur > 15:
                continue  # non-contiguous listing (separate basic-block placement); not a single instruction
            guards[f'{cur:08x}'] = read(cur, nxt - cur).hex()
    a.output.write_text(json.dumps(dict(functions=bodies, instructions=guards), indent=1, sort_keys=True) + '\n')
    print(len(guards))


if __name__ == '__main__':
    main()
