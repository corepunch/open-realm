"""Selected Move producer checks must reject wrong flags, lanes and publication order."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_selected_point_trace import producer,render_header,verify_producer


class SelectedPointTests(unittest.TestCase):
    def setUp(self):
        self.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-selected-point-1.27.json').read_text())
        self.expected=self.fixture['cases'][0]['producer']
        self.rows=copy.deepcopy(self.expected)
        units=['0xaaa0','0xaaa1'];requests=['0x1111','0x2222','0x0']
        for r in self.rows:
            if 'unit' in r:r['unit']=units[r['unit']]
            if 'requests' in r:r['requests']=[requests[v] for v in r['requests']]
            if 'row' in r:r['row'][7]=int(requests[r['row'][7]],16)
            if r['event']=='player-order-variant':
                r['words']=[0]*16;r['words'][5]=r.pop('player')<<8;r['words'][6]=r.pop('flags')
                r['words'][7]=r.pop('order');r['words'][10:12]=r.pop('point');r['words'][12:14]=r.pop('target')

    def test_original_selected_move_producer(self):
        self.assertEqual(verify_producer(self.rows,self.expected),self.expected)

    def test_flags_lanes_members_order_and_admission_cannot_change(self):
        for kind in ('flags','player','point','lane','member','queue','fallback','admission','missing','order'):
            with self.subTest(kind=kind):
                rows=copy.deepcopy(self.rows)
                if kind in ('flags','player','point'):
                    r=next(r for r in rows if r['event']=='player-point-action-begin')
                    if kind=='point':r['point'][0]^=1
                    else:r[kind]^=1
                elif kind=='lane':next(r for r in rows if r['event']=='player-point-attach')['requests'][0]='0x2222'
                elif kind=='member':next(r for r in rows if r['event']=='player-order-publish')['unit']='0xaaa1'
                elif kind=='queue':next(r for r in rows if r['event']=='player-order-publish')['flags']|=1
                elif kind=='fallback':next(r for r in rows if r['event']=='player-order-publish')['fallback']=1
                elif kind=='admission':next(r for r in rows if r['event']=='player-point-target-admit-end')['row'][4]^=1
                elif kind=='missing':rows.pop(4)
                else:rows[2],rows[4]=rows[4],rows[2]
                with self.assertRaises(ValueError):verify_producer(rows,self.expected)

    def test_absolute_input_clocks_are_explicit_and_relative_motion_repeats(self):
        a,b=self.fixture['cases']
        self.assertNotEqual(a['engine_motion'][0][1],b['engine_motion'][0][1])
        self.assertEqual([[r[0],*r[2:]] for r in a['engine_motion']],[[r[0],*r[2:]] for r in b['engine_motion']])
        self.assertEqual(len(a['engine_motion']),228)

    def test_engine_header_keeps_every_original_word(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_selected_point.h').read_text(),render_header(self.fixture))


if __name__=='__main__':unittest.main()
