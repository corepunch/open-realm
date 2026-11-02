#!/usr/bin/env python3
"""Verify bounded AIvi availability byte proof; never runs or modifies the game."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'tools/ghidra/fixtures/research/invisibility327-availability-original.json'
ORIGINAL_SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
BASE = 0x6F000000


def require(condition, message):
    if not condition:
        raise ValueError(message)


class PE:
    def __init__(self, data):
        self.data = data
        require(data[:2] == b'MZ', 'not a PE image')
        pe = struct.unpack_from('<I', data, 60)[0]
        require(data[pe:pe + 4] == b'PE\0\0', 'invalid PE signature')
        count = struct.unpack_from('<H', data, pe + 6)[0]
        size = struct.unpack_from('<H', data, pe + 20)[0]
        require(struct.unpack_from('<I', data, pe + 24 + 28)[0] == BASE, 'wrong image base')
        self.sections = []
        for index in range(count):
            _, va, length, offset = struct.unpack_from('<IIII', data, pe + 24 + size + index * 40 + 8)
            self.sections.append((va, length, offset))

    def read(self, rva, length):
        for va, size, offset in self.sections:
            if va <= rva and rva + length <= va + size:
                result = self.data[offset + rva - va:offset + rva - va + length]
                require(len(result) == length, 'truncated PE section')
                return result
        raise ValueError(f'unbacked RVA range {rva:x}+{length:x}')


def check(fixture, binary, root=ROOT):
    require(fixture['schema'] == 1 and fixture['image_base'] == BASE, 'schema/base mismatch')
    require(fixture['original_sha256'] == ORIGINAL_SHA, 'unexpected original pin')
    require(hashlib.sha256(binary).hexdigest() == ORIGINAL_SHA, 'original DLL SHA mismatch')
    pe = PE(binary)
    functions = fixture['functions']
    require(len(functions) == 48, 'missing/extra guarded function')
    calls = []
    instructions = {}
    for key, function in functions.items():
        rva, size = function['rva'], function['size']
        require(key == f'{rva:06x}', 'function key/RVA mismatch')
        require(int(function['body_start'], 16) == BASE + rva, 'body start mismatch')
        require(int(function['body_end'], 16) + 1 == BASE + rva + size, 'body end mismatch')
        expected = bytes.fromhex(function['bytes'])
        require(len(expected) == size and pe.read(rva, size) == expected, f'body bytes {key}')
        require(hashlib.sha256(expected).hexdigest() == function['sha256'], f'body digest {key}')
        require(bool(function['decompiled_code']), f'missing whole decompilation {key}')
        previous = -1
        for instruction in function['disassembly']:
            address = int(instruction['address'], 16) - BASE
            require(rva <= address < rva + size and address > previous, f'disassembly ordering {key}')
            previous = address
            instructions[address] = instruction
            if instruction['mnemonic'] == 'CALL' and instruction['operands'].startswith('0x'):
                raw = pe.read(address, 5)
                if raw[0] != 0xE8:
                    continue
                target = address + 5 + struct.unpack('<i', raw[1:])[0]
                require(target == int(instruction['operands'], 16) - BASE, f'call target {address:x}')
                calls.append({'rva': address, 'callee_rva': target, 'bytes': raw.hex()})
    require(sorted(calls, key=lambda row: row['rva']) == fixture['direct_calls'], 'missing/extra/reordered call guards')
    require(len(calls) == 135, 'direct call count')
    slots = fixture['vtable_words']
    require(len(slots) == 20, 'vtable count')
    for key, row in slots.items():
        require(key == f"{row['rva']:06x}", 'vtable key')
        raw = pe.read(row['rva'], 4)
        require(raw.hex() == row['bytes'], f'vtable bytes {key}')
        require(struct.unpack('<I', raw)[0] == BASE + row['target_rva'], f'vtable destination {key}')
    for slot, target in {0xCC: 0x3FA480, 0xE0: 0x407600, 0xC4: 0x4076B0,
                         0xE4: 0x40BAB0, 0xC8: 0x40BAD0, 0x264: 0x43B720,
                         0x3DC: 0x3FC460, 0x368: 0x41DC40}.items():
        require(slots[f'{0xB67DB0 + slot:06x}']['target_rva'] == target, 'AIvi slot mapping')
    # These guard the exact static sequence, not runtime taken-branch behavior.
    expected_ops = {
        0x3FBB8A: ('AND', 'dword ptr [ESI + 0x20], 0xffffbfff'),
        0x3F7BB2: ('OR', 'dword ptr [ESI + 0x20], 0x4000'),
        0x40C425: ('CALL', '0x6f67cfb0'),
        0x40C433: ('AND', 'dword ptr [ESI + 0x20], 0xffffbfff'),
        0x40C457: ('CALL', '0x6f1f1ae0'),
        0x40C46A: ('CALL', '0x6f65b6c0'),
        0x65B6D5: ('DEC', 'dword ptr [ESI + 0x84]'),
        0x6909C2: ('MOV', 'dword ptr [ESI + 0x24], EAX'),
        0x6909C8: ('MOV', 'dword ptr [ESI + 0x28], EAX'),
        0x6909FA: ('MOV', 'dword ptr [ESI + 0x38], EAX'),
        0x690A00: ('MOV', 'dword ptr [ESI + 0x3c], EAX'),
        0x40762D: ('AND', 'dword ptr [ESI + 0x20], 0xffffefff'),
        0x407638: ('CALL', 'dword ptr [EDX + 0x388]'),
        0x407651: ('CALL', '0x6f405510'),
        0x3FC466: ('CMP', 'dword ptr [ESI + 0x3c], 0x0'),
        0x69DD0A: ('CMP', 'dword ptr [ESI + 0x40], EAX'),
        0x69DD16: ('CMP', 'dword ptr [ESI + 0x3c], 0x0'),
        0x69DD1C: ('CMP', 'dword ptr [ESI + 0x44], 0x0'),
    }
    for address, operation in expected_ops.items():
        row = instructions[address]
        require((row['mnemonic'], row['operands']) == operation, f'field-operation guard {address:x}')
    evidence = fixture['existing326']
    data = (root / evidence['path']).read_bytes()
    require(hashlib.sha256(data).hexdigest() == evidence['sha256'], '326 source pin mismatch')
    rows = json.loads(data)['private_records']
    snapshots = [{key: row[key] for key in ('seq', 'tick', 'scene', 'rva', 'ability_flags')}
                 for row in rows if row['event'] == 'item-phase' and row['scene'] == 0]
    require(snapshots == evidence['snapshots'], '326 snapshots changed/omitted/reordered')
    require([row['ability_flags'] for row in snapshots] == [0x2020, 0x2021, 0x3021, 0x3021, 0x7021], '326 flag values')
    require(all(not row['ability_flags'] & 0x804 for row in snapshots), '326 wait/contribution flags')
    return {'functions': len(functions), 'vtable_words': len(slots), 'direct_calls': len(calls),
            'existing326_snapshots': len(snapshots), 'live_paused_witness': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True, help='readonly original game.dll')
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    parser.add_argument('--check', action='store_true', help='verify only (also the default; no outputs written)')
    args = parser.parse_args()
    try:
        result = check(json.loads(args.fixture.read_text()), args.binary.read_bytes())
    except (ValueError, KeyError, OSError, struct.error) as error:
        parser.exit(1, f'FAIL: {error}\n')
    print(json.dumps({'status': 'PASS', **result}, sort_keys=True))


if __name__ == '__main__':
    main()
