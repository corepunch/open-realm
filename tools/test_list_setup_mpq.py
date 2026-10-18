#!/usr/bin/env python3
"""Focused structural checks for list_setup_mpq.py."""

import struct
import unittest

from list_setup_mpq import validate_header


def mpq_header(*, archive_size=64, version=0, hash_pos=32, block_pos=48,
               hash_count=1, block_count=1):
    header_size = 44 if version == 1 else 32
    return struct.pack('<4sIIHHIIII', b'MPQ\x1a', header_size, archive_size,
                       version, 0, hash_pos, block_pos, hash_count, block_count)


class ValidateHeaderTests(unittest.TestCase):
    def test_accepts_bounded_classic_header(self):
        data = mpq_header() + bytes(32)
        self.assertEqual(validate_header(data, 0), (64, 32, 48, 1, 1))

    def test_rejects_tables_beyond_declared_archive_even_when_file_has_bytes(self):
        data = mpq_header(archive_size=48, block_pos=48) + bytes(64)
        with self.assertRaisesRegex(ValueError, 'Block table extends beyond'):
            validate_header(data, 0)

    def test_rejects_non_power_of_two_hash_table(self):
        data = mpq_header(hash_count=3, archive_size=128, block_pos=80) + bytes(96)
        with self.assertRaisesRegex(ValueError, 'hash table size'):
            validate_header(data, 0)

    def test_rejects_declared_archive_truncated_by_file(self):
        data = mpq_header(archive_size=96) + bytes(32)
        with self.assertRaisesRegex(ValueError, 'extends beyond file'):
            validate_header(data, 0)


if __name__ == '__main__':
    unittest.main()
