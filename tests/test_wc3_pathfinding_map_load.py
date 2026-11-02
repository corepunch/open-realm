"""Partial file loads, stale padding and detached numeric expectations cannot certify MAP-02.1."""
import copy
import base64
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_map_load_trace import expand,verify_load,verify_bridge_terrain,verify_bridge_saved


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


class BridgeTerrainTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/research/MAP-02.2-expected.json').read_text())
        cls.bundle=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-bridge-terrain-inputs-1.27.json.gz').read_bytes()))

    def change_rows(self,change):
        bundle=copy.deepcopy(self.bundle);fixture=copy.deepcopy(self.fixture)
        name='observe-authored-v4-1.jsonl'
        rows=[json.loads(s) for s in base64.b64decode(bundle['files'][name]).decode().splitlines()]
        change(rows)
        raw=('\n'.join(json.dumps(r) for r in rows)+'\n').encode()
        bundle['files'][name]=base64.b64encode(raw).decode()
        bundle['sha256'][name]=fixture['captures'][name]=hashlib.sha256(raw).hexdigest()
        return fixture,bundle

    def test_four_complete_file_loads_and_observer_free_control(self):
        self.assertEqual(verify_bridge_terrain(self.fixture,self.bundle),
            dict(bridge_file_loads=4,bridge_terrain_cells=16384,bridge_control_markers=193))

    def test_ghidra_evidence_is_saved_and_complete(self):
        payload=json.loads((ROOT/'tools/ghidra/fixtures/retail-bridge-terrain-ghidra-1.27.json').read_text())
        self.assertEqual(verify_bridge_saved(payload),1)
        payload['unsaved']=True
        with self.assertRaisesRegex(ValueError,'not saved'):verify_bridge_saved(payload)

    def test_damaged_original_capture_is_rejected(self):
        bundle=copy.deepcopy(self.bundle)
        name='observe-authored-v4-1.jsonl'
        bundle['files'][name]=base64.b64encode(base64.b64decode(bundle['files'][name])+b'\n').decode()
        with self.assertRaisesRegex(ValueError,'capture hash'):
            verify_bridge_terrain(self.fixture,bundle)

    def test_missing_observer_completion_is_rejected(self):
        fixture,bundle=self.change_rows(lambda rows:rows.__setitem__(slice(None),
            [r for r in rows if r['event']!='trace-end']))
        with self.assertRaisesRegex(ValueError,'observer incomplete'):
            verify_bridge_terrain(fixture,bundle)

    def test_bridge_cannot_clear_authored_walk_in_start_snapshot(self):
        def clear(rows):
            start=next(r for r in rows if r['event']=='cell-snapshot' and 'label=start ' in r['marker'])
            start['snapshot']['words'][20*64+32]&=~0x02000000
        fixture,bundle=self.change_rows(clear)
        with self.assertRaisesRegex(ValueError,'changes authored terrain'):
            verify_bridge_terrain(fixture,bundle)

    def test_numeric_engine_fixture_is_bound_to_unchanged_loads(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_bridge_terrain.h').read_text()
        table=source.split('retail_bridge_terrain_runs[][2]={',1)[1].split('};',1)[0]
        runs=[[int(n),int(v)] for n,v in re.findall(r'\{(\d+),(\d+)\}',table)]
        table=source.split('retail_bridge_terrain_cases[][2]={',1)[1].split('};',1)[0]
        spans=[[int(n),int(v)] for n,v in re.findall(r'\{(\d+),(\d+)\}',table)]
        for name,(offset,count) in zip(('observe-authored-v4-1','observe-blank-v4-1','observe-deckwalk-v5-1'),spans,strict=True):
            rows=[json.loads(s) for s in base64.b64decode(self.bundle['files'][name+'.jsonl']).decode().splitlines()]
            loaded=next(r for r in rows if r['event']=='map-load-complete')['snapshot']
            self.assertEqual(expand(runs[offset:offset+count],4096),[w>>24 for w in loaded['words']])


if __name__=='__main__':unittest.main()
