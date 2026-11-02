"""Retained production storage against complete frozen original requests."""
import copy
import ctypes
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_fine_storage_trace import verify


class Objects(ctypes.Structure):
    _fields_=[('cells',ctypes.POINTER(ctypes.c_uint8)),('objects',ctypes.POINTER(ctypes.c_uint32))]


class FineStorageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-fine-storage-1.27.json').read_text())
        cls.live=json.loads((ROOT/'tools/ghidra/fixtures/retail-fine-storage-live-1.27.json').read_text())

    def test_all_original_nodes_routes_and_capacities_at_both_optimizations(self):
        with tempfile.TemporaryDirectory(prefix='wc3-fine-storage-') as tmp:
            cells=(ctypes.c_uint8*65536)(*[2 if x>=192 or (abs(x-128)<=8 and abs(y-128)<=8) else 0 for y in range(256) for x in range(256)])
            for opt in ('-O0','-O2'):
                lib=Path(tmp)/(opt+'.so')
                subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-DBZ_WC3_FINE_TRACE',opt,'-fPIC','-shared','-I',str(ROOT),str(ROOT/'tools/ghidra/wc3_pathing_engine_probe.c'),'-o',str(lib)],check=True)
                engine=ctypes.CDLL(str(lib))
                engine.pathing_fine_request_words.argtypes=[ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(Objects),ctypes.POINTER(ctypes.c_uint32)]
                engine.pathing_fine_storage.argtypes=[ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32)]
                engine.pathing_fine_node_state.argtypes=[ctypes.POINTER(ctypes.c_uint32)]
                for row in self.fixture['cases']:
                    with self.subTest(opt=opt,case=row['name']):
                        words=[struct.unpack('<I',struct.pack('<f',v))[0] for v in (*row['start'],*row['goal'])]
                        query=(ctypes.c_uint32*15)(256,256,*map(int,row['start']),*map(int,row['goal']),row['budget'],0,0x02000002,0,0,*words)
                        out=(ctypes.c_uint32*65542)()
                        engine.pathing_fine_request_words(query,ctypes.byref(Objects(cells,None)),out)
                        self.assertEqual(list(out[:4]),[row['result'],row['work'],row['nodes'],len(row['route_words'])//2])
                        self.assertEqual(list(out[6:6+len(row['route_words'])]),row['route_words'])
                        state=(ctypes.c_uint32*(1+7*32768))();engine.pathing_fine_node_state(state)
                        normalized=[list(state[1+7*i:8+7*i]) for i in range(state[0])]
                        self.assertEqual(hashlib.sha256(json.dumps(normalized,separators=(',',':')).encode()).hexdigest(),row['node_sha256'])
                        capacity=(ctypes.c_uint32*3)();engine.pathing_fine_storage(0,capacity)
                        self.assertEqual(list(capacity),[row['node_capacity'],row['heap_capacity'],row['nodes']])
                capacity=(ctypes.c_uint32*3)();engine.pathing_fine_storage(1,capacity)
                self.assertEqual(list(capacity),[0,0,0])

    def test_engine_header_retains_every_original_route_word(self):
        source=(ROOT/'games/warcraft-3/game/tests/retail_fine_storage.h').read_text()
        for i,row in enumerate(self.fixture['cases']):
            table=source.split(f'retail_storage_route_{i}[][2]={{',1)[1].split('};',1)[0]
            self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',table)],row['route_words'])
        self.assertEqual(re.findall(r'UINT64_C\(0x([0-9a-f]+)\)',source),[r['node_fnv64'] for r in self.fixture['cases']])

    def test_metadata_free_list_is_distinct_from_request_node_capacity(self):
        self.assertEqual(self.fixture['cases'][1]['nodes'],32768)
        self.assertEqual(self.fixture['compaction']['free_slots'],32769)
        self.assertEqual(self.fixture['capacity_refusal']['new'],0xffffffff)
        self.assertEqual(self.fixture['capacity_refusal']['existing'],0)
        self.assertEqual(self.fixture['cases'][-1]['metadata_count'],108)
        self.assertEqual(self.fixture['cases'][-1]['link_count'],32769)

    def test_live_witness_rejects_changed_table_identity_or_growth_words(self):
        capture=self.live['captures'][0]
        rows=[dict(event='metadata',**capture['metadata']),dict(event='trace-end',installed=True)]
        for observed in self.live['observations']:
            row=copy.deepcopy(observed)
            if row['event']=='fine-storage-constructed':row['fine']='0x10000000'
            else:row['table']='0x10000024' if row['kind']=='nodes' else '0x10000044'
            rows.insert(-1,row)
        self.assertEqual(verify(rows,self.live,capture),self.live['observations'])
        for change in ('table','growth','source','end','missing'):
            bad=copy.deepcopy(rows)
            if change=='table':bad[1]['table']='0x10000064'
            elif change=='growth':bad[1]['after']['capacity']=2048
            elif change=='source':bad[0]['source_sha256']['wc3_pathfinding.js']='0'*64
            elif change=='end':bad.pop()
            else:bad.pop(1)
            with self.subTest(change=change),self.assertRaises(ValueError):verify(bad,self.live,capture)


if __name__=='__main__':unittest.main()
