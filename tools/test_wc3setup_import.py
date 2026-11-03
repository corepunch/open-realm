#!/usr/bin/env python3
"""Synthetic import regression fixtures (user-run, no retail data required).

Run after building both native tools:
    python3 tools/test_wc3setup_import.py build/bin/isoextract build/bin/wc3setup
"""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

FREE = 0xFFFFFFFF
EXISTS = 0x80000000
CRYPT = [0] * 0x500
seed = 0x100001
for i in range(0x100):
    for j in range(i, 0x500, 0x100):
        seed = (seed * 125 + 3) % 0x2AAAAB
        upper = (seed & 0xFFFF) << 16
        seed = (seed * 125 + 3) % 0x2AAAAB
        CRYPT[j] = upper | (seed & 0xFFFF)


def mpq_hash(name, kind):
    a, b = 0x7FED7FED, 0xEEEEEEEE
    for c in name.replace('/', '\\').upper().encode('ascii'):
        a = (CRYPT[kind * 256 + c] ^ ((a + b) & FREE)) & FREE
        b = (c + a + b + (b << 5) + 3) & FREE
    return a


def encrypt(data, key):
    # MPQ table encryption uses the *plaintext* to update the running seed.
    out = bytearray()
    seed = 0xEEEEEEEE
    for (plain,) in struct.iter_unpack('<I', data):
        seed = (seed + CRYPT[0x400 + (key & 255)]) & FREE
        out += struct.pack('<I', plain ^ ((key + seed) & FREE))
        key = (((~key << 21) + 0x11111111) | (key >> 11)) & FREE
        seed = (plain + seed + (seed << 5) + 3) & FREE
    return bytes(out)


def mpq(files):
    """Build small, valid, uncompressed classic MPQ with no (listfile)."""
    n_hashes = 1
    while n_hashes < max(8, len(files) * 2):
        n_hashes *= 2
    hashes = [(0, 0, 0, FREE) for _ in range(n_hashes)]
    blocks = []
    payloads = bytearray()
    for name, body in files:
        offset = 32 + len(payloads)
        payloads.extend(body)
        blocks.append((offset, len(body), len(body), EXISTS))
        slot = mpq_hash(name, 0) & (n_hashes - 1)
        while hashes[slot][3] != FREE:
            slot = (slot + 1) & (n_hashes - 1)
        hashes[slot] = (mpq_hash(name, 1), mpq_hash(name, 2), 0, len(blocks) - 1)
    hash_pos = 32 + len(payloads)
    block_pos = hash_pos + n_hashes * 16
    archive_size = block_pos + len(blocks) * 16
    header = struct.pack('<4sIIHHIIII', b'MPQ\x1a', 32, archive_size, 0, 0,
                         hash_pos, block_pos, n_hashes, len(blocks))
    hash_table = b''.join(struct.pack('<IIII', *v) for v in hashes)
    block_table = b''.join(struct.pack('<IIII', *v) for v in blocks)
    return (header + payloads + encrypt(hash_table, 0xC3AF3770)
            + encrypt(block_table, 0xEC83B3A3))


def iso_record(extent, size, name, is_dir=False):
    rec = bytearray(33 + len(name) + (len(name) % 2 == 0))
    rec[0] = len(rec)
    rec[2:10] = struct.pack('<I', extent) + struct.pack('>I', extent)
    rec[10:18] = struct.pack('<I', size) + struct.pack('>I', size)
    rec[25] = 2 if is_dir else 0
    rec[28:32] = struct.pack('<H', 1) + struct.pack('>H', 1)
    rec[32] = len(name)
    rec[33:33 + len(name)] = name
    return rec


def iso_with_nested_setup(payload):
    sectors = [bytearray(2048) for _ in range(20 + (len(payload) + 2047) // 2048)]
    pvd = sectors[16]
    pvd[0] = 1
    pvd[1:6] = b'CD001'
    pvd[6] = 1
    pvd[156:190] = iso_record(18, 2048, b'\x00', True)
    sectors[17][0] = 255
    sectors[17][1:6] = b'CD001'
    sectors[17][6] = 1
    root = (iso_record(18, 2048, b'\x00', True)
            + iso_record(18, 2048, b'\x01', True)
            + iso_record(19, 2048, b'DATA', True))
    sectors[18][:len(root)] = root
    directory = (iso_record(19, 2048, b'\x00', True)
                 + iso_record(18, 2048, b'\x01', True)
                 + iso_record(20, len(payload), b'SETUP.MPQ;1'))
    sectors[19][:len(directory)] = directory
    image = b''.join(sectors)
    return image[:20 * 2048] + payload + image[20 * 2048 + len(payload):]


class TestImporter(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.iso_tool = str(pathlib.Path(sys.argv[1]).resolve())
        cls.setup_tool = str(pathlib.Path(sys.argv[2]).resolve())

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def invoke(self, tool, *args):
        return subprocess.run([tool, *map(str, args)], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True)

    @staticmethod
    def expansion():
        return mpq([
            ('Melee_V0\\Units\\UnitBalance.slk', b'x'),
            ('Custom_V0\\Units\\UnitBalance.slk', b'y'),
            ('Custom_V1\\Units\\UnitBalance.slk', b'z'),
        ])

    def test_iso_targeted_recursive_member_and_missing_member(self):
        data = mpq([('SetupDat\\maps.lst', b'dummy')])
        image = self.root / 'disc.iso'
        image.write_bytes(iso_with_nested_setup(data))
        output = self.root / 'stage' / 'Setup.mpq'
        proc = self.invoke(self.iso_tool, 'extract-file', image, 'setup.mpq', output)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(output.read_bytes(), data)
        proc = self.invoke(self.iso_tool, 'extract-file', image, 'War3x.mpq', output)
        self.assertEqual(proc.returncode, 2, proc.stderr)

    def test_anonymous_archive_and_map_detection(self):
        embedded = self.expansion()
        installer = mpq([
            ('File00000155.w3x', embedded),
            ('SetupDat\\maps.lst', b'dummy'),
        ])
        setup = self.root / 'Setup.mpq'
        setup.write_bytes(installer)
        self.assertEqual(self.invoke(self.setup_tool, 'maps', setup).returncode, 0)
        out = self.root / 'archives'
        proc = self.invoke(self.setup_tool, 'extract', setup, out)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual((out / 'War3x.mpq').read_bytes(), embedded)
        self.assertEqual(self.invoke(self.setup_tool, 'verify', out / 'War3x.mpq').returncode, 0)

    def test_duplicate_anonymous_identity_is_not_guessed(self):
        embedded = self.expansion()
        setup = self.root / 'Setup.mpq'
        setup.write_bytes(mpq([('a.bin', embedded), ('b.bin', embedded)]))
        out = self.root / 'out'
        proc = self.invoke(self.setup_tool, 'extract', setup, out)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertFalse((out / 'War3x.mpq').exists())

    def test_explicitly_named_member(self):
        embedded = self.expansion()
        setup = self.root / 'Setup.mpq'
        setup.write_bytes(mpq([('War3x.mpq', embedded)]))
        out = self.root / 'out'
        proc = self.invoke(self.setup_tool, 'extract', setup, out)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual((out / 'War3x.mpq').read_bytes(), embedded)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('Usage: python3 tools/test_wc3setup_import.py <isoextract> <wc3setup>')
    unittest.main(argv=[sys.argv[0]])
