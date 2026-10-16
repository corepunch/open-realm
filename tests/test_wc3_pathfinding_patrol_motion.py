"""Patrol legs replay ordinary point Move journeys and reverse at an enclosed endpoint."""
import copy
import json
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_patrol_motion_trace import verify_contract,render_header


def word(value):return struct.unpack('<I',struct.pack('<f',value))[0]


class PatrolMotionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-patrol-motion-1.27.json').read_text())
    def spec(self):return copy.deepcopy(self.fixture)
    def test_repeated_captures_are_one_journey(self):
        verify_contract(self.fixture)
        self.assertEqual(len(self.fixture['captures']),2)
        self.assertNotEqual(*[c['sha256'] for c in self.fixture['captures']])
    def test_patrol_head_is_the_two_point_order(self):
        s=self.spec();s['samples'][1][40][0]=851990
        with self.assertRaises(ValueError):verify_contract(s)
    def test_patrol_leg_cannot_leave_the_move_journey(self):
        s=self.spec();s['samples'][3][10][3]=word(1216.5)
        with self.assertRaises(ValueError):verify_contract(s)
    def test_enclosed_endpoint_must_reverse(self):
        s=self.spec()
        for t in range(31,80):s['samples'][8][t][3]=word(1094.0)
        with self.assertRaises(ValueError):verify_contract(s)
    def test_literal_header_is_the_frozen_motion(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_patrol_motion.h').read_text(),render_header(self.fixture))


if __name__=='__main__':unittest.main()
