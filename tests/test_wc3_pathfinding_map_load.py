"""Partial file loads, stale padding and detached numeric expectations cannot certify MAP-02.1."""
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_map_load_trace import expand,verify_load


class MapLoadTests(unittest.TestCase):
    def setUp(self):
        self.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-map-load-1.27.json').read_text())
        geometry=self.fixture['geometry']
        load={k:v for k,v in geometry.items() if k not in ('cell_runs','hierarchy')}
        load['event']='map-load-complete';load['cells']=expand(geometry['cell_runs'],384*256)
        load['hierarchy']=[dict(level=h['level'],width=h['width'],height=h['height'],
                              values=[[(v>>s)&3 for s in (6,4,2,0)] for v in expand(h['runs'],h['width']*h['height'])])
                           for h in geometry['hierarchy']]
        self.rows=[dict(event='maps',maps=copy.deepcopy(self.fixture['initial_maps'])),load,
                   dict(event='trace-end',installed=True,counts={'map-load-complete':1})]

    def test_complete_public_file_load(self):
        self.assertEqual(verify_load(self.rows,self.fixture),dict(file_loads=1,fine_cells=98304,hierarchy_classes=145848))

    def test_missing_file_loader_or_constructor(self):
        for at in (0,1):
            with self.assertRaisesRegex(ValueError,'loader/constructor'):
                verify_load(self.rows[:at]+self.rows[at+1:],self.fixture)

    def test_wrong_file_or_world_bounds(self):
        for key in ('filename','worldBounds'):
            rows=copy.deepcopy(self.rows);rows[1][key]=None
            with self.assertRaisesRegex(ValueError,'field differs'):
                verify_load(rows,self.fixture)

    def test_changed_decoded_fine_cell(self):
        self.rows[1]['cells'][0]^=2
        with self.assertRaisesRegex(ValueError,'decoded fine'):
            verify_load(self.rows,self.fixture)

    def test_changed_padding_or_parent_class(self):
        for level in range(4):
            rows=copy.deepcopy(self.rows);rows[1]['hierarchy'][level]['values'][-1][0]^=1
            with self.assertRaisesRegex(ValueError,'hierarchy classes'):
                verify_load(rows,self.fixture)

    def test_wrong_initial_dimensions(self):
        self.rows[0]['maps'][2]['dimensions']=[192,128]
        with self.assertRaisesRegex(ValueError,'dimensions or scales'):
            verify_load(self.rows,self.fixture)

    def test_truncated_or_different_wpm_bytes(self):
        for change in ('hash','extent'):
            fixture=copy.deepcopy(self.fixture)
            if change=='hash':fixture['wpm']['sha256']='0'*64
            else:fixture['wpm']['runs'].pop()
            with self.assertRaises(ValueError):verify_load(self.rows,fixture)

    def test_unfinished_capture(self):
        with self.assertRaisesRegex(ValueError,'did not finish'):
            verify_load(self.rows[:-1],self.fixture)

    def test_literal_engine_inputs_and_outputs_equal_original_capture(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_map_load.h').read_text()
        for name,expected in [('retail_load_wpm_runs',self.fixture['wpm']['runs']),
                              ('retail_load_fine_runs',self.fixture['geometry']['cell_runs']),
                              ('retail_load_class_runs',[r for h in self.fixture['geometry']['hierarchy'] for r in h['runs']])]:
            table=source.split(name,1)[1].split('={',1)[1].split('};',1)[0]
            actual=[[int(n),int(v)] for n,v in re.findall(r'\{(\d+),(\d+)\}',table)]
            self.assertEqual(actual,expected)
        header=struct.pack('<4I',*self.fixture['wpm']['header'])
        raw=bytes(expand(self.fixture['wpm']['runs'],384*256))
        self.assertEqual(hashlib.sha256(header+raw).hexdigest(),self.fixture['wpm']['sha256'])


if __name__=='__main__':unittest.main()
