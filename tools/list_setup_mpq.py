#!/usr/bin/env python3
"""List MPQ block entries and recover names from candidate filename lists.

Usage:
    python3 tools/list_setup_mpq.py ./data/war3cds/Setup.mpq
    python3 tools/list_setup_mpq.py ./data/war3cds/Setup.mpq --names-file names.txt

MPQ block tables contain offsets, sizes and flags, not filenames. If an archive
has no usable (listfile), names can only be recovered by matching candidate
filenames against its stored filename hashes. A names file has one path per line.
"""

import argparse
import mmap
import struct
from pathlib import Path

MASK = 0xFFFFFFFF
EXISTS = 0x80000000


def crypt_table():
    table = [0] * 0x500
    seed = 0x00100001
    for index in range(0x100):
        for group in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            upper = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            table[index + group * 0x100] = upper | (seed & 0xFFFF)
    return table


def hash_string(value, table, hash_type):
    seed1, seed2 = 0x7FED7FED, 0xEEEEEEEE
    for ch in value.replace('/', '\\').upper().encode('ascii'):
        seed1 = (table[hash_type * 0x100 + ch] ^ (seed1 + seed2)) & MASK
        seed2 = (ch + seed1 + seed2 + (seed2 << 5) + 3) & MASK
    return seed1


def decrypt_words(encrypted, key, table):
    seed = 0xEEEEEEEE
    for (word,) in struct.iter_unpack('<I', encrypted):
        seed = (seed + table[0x400 + (key & 0xFF)]) & MASK
        decoded = (word ^ (key + seed)) & MASK
        key = (((~key << 21) + 0x11111111) | (key >> 11)) & MASK
        seed = (decoded + seed + (seed << 5) + 3) & MASK
        yield decoded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mpq', type=Path, help='Path to Setup.mpq (or any classic MPQ)')
    parser.add_argument('--all', action='store_true', help='Also show unused block table slots')
    parser.add_argument('--names-file', type=Path,
                        help='Candidate MPQ paths, one per line; matching hashes recover names')
    args = parser.parse_args()

    with args.mpq.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        # Warcraft III installers normally have an MPQ header at byte zero.
        base = data.find(b'MPQ\x1a', 0, min(len(data), 1024 * 1024))
        if base < 0 or base + 32 > len(data):
            parser.error('No MPQ header found in the first MiB')
        signature, header_size, archive_size, version, sector_shift, hash_pos, block_pos, hash_count, block_count = struct.unpack_from('<4sIIHHIIII', data, base)
        if version > 1 or header_size < 32 or block_count > 1_000_000:
            parser.error('Unsupported or invalid MPQ header')
        table_start = base + block_pos
        table_end = table_start + block_count * 16
        if table_end > len(data):
            parser.error('Block table extends beyond file')
        table = crypt_table()
        words = list(decrypt_words(data[table_start:table_end],
                                   hash_string('(block table)', table, 3), table))
        blocks = [tuple(words[i:i + 4]) for i in range(0, len(words), 4)]
        recovered = {}

        if args.names_file:
            hash_start = base + hash_pos
            hash_end = hash_start + hash_count * 16
            if hash_end > len(data):
                parser.error('Hash table extends beyond file')
            hash_words = list(decrypt_words(data[hash_start:hash_end],
                                            hash_string('(hash table)', table, 3), table))
            candidates = {}
            for name in args.names_file.read_text(encoding='utf-8-sig').splitlines():
                name = name.strip()
                if not name or name.startswith('#'):
                    continue
                try:
                    pair = (hash_string(name, table, 1), hash_string(name, table, 2))
                except UnicodeEncodeError:
                    continue
                candidates.setdefault(pair, []).append(name.replace('/', '\\'))

            for i in range(0, len(hash_words), 4):
                name_a, name_b, _locale_platform, block_index = hash_words[i:i + 4]
                if block_index in (0xFFFFFFFF, 0xFFFFFFFE) or block_index >= len(blocks):
                    continue
                for name in candidates.get((name_a, name_b), ()):
                    recovered.setdefault(block_index, []).append(name)

        print(f'MPQ header offset: {base}, archive size: {archive_size}, block entries: {block_count}')
        if args.names_file:
            print(f'Recovered names: {len(recovered)} block(s) matched from {args.names_file}')
        print('index  offset       packed_size  file_size    flags       type  candidate name(s)')
        occupied = 0
        for index, (offset, packed_size, file_size, flags) in enumerate(blocks):
            if not args.all and not flags & EXISTS:
                continue
            if flags & EXISTS:
                occupied += 1
            kind = 'MPQ' if flags & EXISTS and not flags & 0x10000 and packed_size >= 4 and base + offset + 4 <= len(data) and data[base + offset:base + offset + 4] == b'MPQ\x1a' else '-'
            names = ', '.join(recovered.get(index, ()))
            print(f'{index:5}  {offset:10}  {packed_size:11}  {file_size:11}  0x{flags:08x}  {kind:4}  {names}')
        print(f'Occupied entries: {occupied}/{block_count}')


if __name__ == '__main__':
    main()
