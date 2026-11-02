#!/usr/bin/env python3
"""Check the retail raw target-token table against typed engine SLK parsing.

The native table and all 63 parser instructions are frozen from the pinned DLL.
This is a static contract; Captain selector/runtime policy is verified separately.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
SPEC = Path('tools/ghidra/fixtures/retail-target-tokens-1.27.json')


def verify(spec, root=ROOT, binary=None):
    rows = spec['tokens']
    table = bytes.fromhex(spec['table_bytes'])
    assert len(rows) == 51 and len(table) == 51 * 8, 'incomplete target table'
    assert len({r['token'] for r in rows}) == 51, 'duplicate token'
    table_address = int(spec['table_address'], 16)
    for i, row in enumerate(rows):
        assert int(row['address'], 16) == table_address + i * 8, 'table address'
        assert struct.unpack_from('<II', table, i * 8) == (int(row['pointer'], 16), row['mask']), 'table word'
        assert bytes.fromhex(row['string_bytes']) == row['token'].encode('ascii') + b'\0', 'token bytes'
    instructions = spec['parser_instructions']
    assert len(instructions) == 63, 'incomplete parser instructions'
    code = b''.join(bytes.fromhex(g['bytes']) for g in instructions)
    assert hashlib.sha256(code).hexdigest() == spec['instruction_sha256'], 'parser byte digest'
    previous = int(spec['parser_address'], 16)
    for guard in instructions:
        address = int(guard['address'], 16)
        assert address >= previous, 'overlapping parser guards'
        previous = address + len(bytes.fromhex(guard['bytes']))
    expected = [(r['token'], r['mask']) for r in rows]
    source = root / 'games/warcraft-3/game/g_metadata.c'
    body = source.read_text().split('static uint32_t ParseWeaponTargetMask', 1)[1].split('};', 1)[0]
    actual = [(name, int(bits, 16)) for name, bits in re.findall(r'\{"([a-z_]+)",\s*(0x[0-9a-f]+)u\}', body)]
    assert actual == expected, 'engine authored token table differs from original'
    test = (root / 'games/warcraft-3/game/tests/t_captain376_mask.c').read_text()
    cases = [(name, int(bits, 16)) for name, bits in re.findall(r'\{"([a-z_]+)",\s*(0x[0-9a-f]+)u\}', test)]
    assert cases == expected, 'typed SLK regression differs from original'
    if binary:
        sys.path.insert(0, str(root / 'tools'))
        from extract_wc3_ability_classes import PE
        data = Path(binary).read_bytes()
        assert hashlib.sha256(data).hexdigest() == spec['game_sha256'], 'wrong retail binary'
        pe = PE(data)
        def read(address, size):
            offset = pe.offset(address)
            return data[offset:offset + size]
        assert read(table_address, len(table)) == table, 'original table bytes'
        for row in rows:
            value = bytes.fromhex(row['string_bytes'])
            assert read(int(row['pointer'], 16), len(value)) == value, 'original token string'
        for guard in instructions:
            value = bytes.fromhex(guard['bytes'])
            assert read(int(guard['address'], 16), len(value)) == value, 'original instruction bytes'
    return {'passed': True, 'tokens': 51, 'parser_instructions': 63,
            'original_binary_checked': bool(binary), 'binary_sha256': spec['game_sha256'] if binary else None, 'scope': spec['scope']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = verify(json.loads((ROOT / SPEC).read_text()), binary=args.binary)
    text = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
