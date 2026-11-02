"""A partial adaptive plan must retain its complete public failure journey."""
import copy
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from make_wc3_pathfinding_map import passage_map_member, resize_units
from verify_wc3_adaptive_passage_trace import render_header, verify_contract


class AdaptivePassageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-adaptive-passage-1.27.json').read_text())

    def test_native_motion_is_the_engine_literal(self):
        verify_contract(self.fixture)
        self.assertEqual(len(self.fixture['cases']),2)
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_adaptive_passage.h').read_text(),render_header(self.fixture))

    def test_truncated_partial_plan_cannot_certify_failure(self):
        s=copy.deepcopy(self.fixture)
        next(r for r in s['lifecycle'] if r['event']=='route')['truncated']=True
        with self.assertRaises(ValueError):verify_contract(s)

    def test_coarse_failure_cannot_be_replaced_with_success(self):
        s=copy.deepcopy(self.fixture)
        next(r for r in s['lifecycle'] if r['event']=='search')['result']=1
        with self.assertRaises(ValueError):verify_contract(s)

    def test_retry_must_retain_its_terminal_count(self):
        s=copy.deepcopy(self.fixture)
        [r for r in s['lifecycle'] if r['event']=='retry-result'][-1]['after']=0
        with self.assertRaises(ValueError):verify_contract(s)

    def test_failure_cannot_be_reported_at_the_clicked_goal(self):
        s=copy.deepcopy(self.fixture)
        s['motion'][-1][2]=struct.unpack('<I',struct.pack('<f',54.5))[0]
        with self.assertRaises(ValueError):verify_contract(s)

    def test_failure_must_reclaim_the_public_order(self):
        s=copy.deepcopy(self.fixture)
        next(r for r in s['lifecycle'] if r['event']=='task-cant-path')['after']['orderHead']=[1,2]
        with self.assertRaises(ValueError):verify_contract(s)

    def test_file_backed_terrain_has_exact_reduced_cells(self):
        wpm=passage_map_member('war3map.wpm',b'')
        self.assertEqual(wpm[:16],b'MP3W'+struct.pack('<III',0,64,64))
        expected=bytes(0xc6 if self.fixture['input_rows'][y//2]&(1<<(x//2)) else 0 for y in range(64) for x in range(64))
        self.assertEqual(wpm[16:],expected)
        self.assertEqual(expected.count(0xc6),72)

    def test_flat_terrain_keeps_the_existing_tileset_tables(self):
        prefix=b'W3E!'+struct.pack('<I',11)+b'X'+struct.pack('<II',0,2)+b'LdrtLgrs'+struct.pack('<I',1)+b'CLdi'
        terrain=passage_map_member('war3map.w3e',prefix+struct.pack('<IIff',3,3,-32,-64)+b'old vertices')
        self.assertEqual(terrain[:len(prefix)],prefix)
        self.assertEqual(struct.unpack_from('<IIff',terrain,len(prefix)),(17,17,0,0))
        self.assertEqual(len(terrain),len(prefix)+16+289*7)
        self.assertEqual(set(struct.iter_unpack('<HHBBB',terrain[len(prefix)+16:])),{(8192,8192,0,0,2)})

    def test_collision_clone_preserves_original_tables_and_rejects_reuse(self):
        source=struct.pack('<III',1,0,0)
        clone=resize_units(source,clones=[(b'hV80',40.)])
        self.assertEqual(clone,struct.pack('<III',1,0,1)+b'hfoohV80'+struct.pack('<I',1)+b'ucol'+struct.pack('<If',2,40.)+b'hV80')
        with self.assertRaises(ValueError):resize_units(clone,clones=[(b'hV80',40.)])


if __name__=='__main__':unittest.main()
