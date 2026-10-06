"""Public range controls preserve the deliberately incomplete parent task."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_captain_ranges_trace import range_state,verify_contract
from make_wc3_pathfinding_map import captain_range_units


class CaptainRanges(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-captain-ranges-1.27.json').read_text())

    def test_contract(self):
        self.assertTrue(verify_contract(self.fixture)['passed'])

    def test_repeated_original_archives(self):
        for a in self.fixture['archives']:
            data=(ROOT/a['path']).read_bytes();self.assertEqual(hashlib.sha256(data).hexdigest(),a['sha256'])
            raw=gzip.decompress(data);self.assertEqual(hashlib.sha256(raw).hexdigest(),a['uncompressed_sha256'])
            rows=[json.loads(x)for x in raw.splitlines()]
            self.assertEqual(range_state(rows),self.fixture['state'])
            self.assertEqual(rows[0]['source_sha256'],self.fixture['source_sha256'])

    def test_saved_typed_ghidra(self):
        g=self.fixture['ghidra'];p=ROOT/'tools/ghidra/fixtures'/g['readback'];data=p.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(),g['sha256']);r=json.loads(data)
        self.assertFalse(r['unsaved_changes']);self.assertEqual(len(r['functions']),5)
        f=r['functions'][0];self.assertEqual(f['name'],'Attack_HasLongRangeSiegeSlot')
        self.assertEqual(f['parameters'][0]['storage'],'ECX:4')
        self.assertTrue(any('CMP dword ptr [ESI + 0xf4],0x3' in x for x in f['instructions']))

    def test_reject_wrong_bonus_disabled_or_fresh_range(self):
        for index,word in [(2,1),(4,4),(6,4)]:
            f=copy.deepcopy(self.fixture);f['state']['authored'][index][word]^=1
            with self.assertRaises(ValueError):verify_contract(f)

    def test_reject_boundary_or_wrong_delivery_enum(self):
        for field in (0,1,2,3,4,5):
            f=copy.deepcopy(self.fixture)
            row=f['state']['slots'][0]
            if isinstance(row[field],list):row[field][0]^=1
            else:row[field]^=1
            with self.assertRaises(ValueError):verify_contract(f)

    def test_reject_stale_reused_mover_range(self):
        f=copy.deepcopy(self.fixture);f['state']['initial_arrivals'][6]=f['state']['initial_arrivals'][4][:]
        with self.assertRaises(ValueError):verify_contract(f)

    def test_builder_has_authored_acquisition_and_damage_types(self):
        import struct
        data=captain_range_units(struct.pack('<III',1,0,0))
        self.assertEqual(struct.unpack_from('<III',data),(1,0,6))
        self.assertIn(b'ua1t'+struct.pack('<I',3)+b'siege\0hCR2',data)
        self.assertIn(b'uacq'+struct.pack('<If',2,1000.)+b'hCR0',data)
        self.assertIn(b'ua1r'+struct.pack('<II',0,600)+b'hCR1',data)


if __name__=='__main__':unittest.main()
