"""Passage engine expectations stay attached to original full request/footprint outputs."""
import json
import hashlib
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[1]


class PassageTests(unittest.TestCase):
    def setUp(self):
        self.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-passage-matrix-1.27.json').read_text())
        self.source=(ROOT/'games/warcraft-3/game/tests/retail_passages.h').read_text()

    def test_every_native_endpoint_and_complete_route_is_preserved(self):
        table=self.source.split('retail_passages[]={',1)[1].split('};',1)[0]
        rows=[line for line in table.splitlines() if line.strip()]
        middles=self.source.split('retail_passage_middle[]={',1)[1].split('};',1)[0]
        middles=[int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',middles)]
        shapes=self.source.split('retail_passage_shapes[][16]={',1)[1].split('};',1)[0]
        shapes=[[int(v,16) for v in re.findall(r'0x([0-9a-f]+)',line)] for line in shapes.splitlines() if line.strip()]
        self.assertEqual(len(rows),3200)
        for row,case in zip(rows,self.fixture['cases']):
            words=[int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',row)]
            self.assertEqual(words,case['source_words']+case['target_words']+case['route_words'][:2])
            fields=[int(v) for v in re.findall(r'\d+',row.split('},',1)[1])]
            shape,cls,mask,flags,a,b,result,count,middle=fields
            self.assertEqual([cls,mask,flags,a,b,result,count],
                [case['size_class'],case['mask']>>24,case['terrain_flags'],*case['endpoints'],case['result'],len(case['route_words'])//2])
            geometry=bytes.fromhex(self.fixture['maps'][case['fixture']])
            self.assertEqual(shapes[shape],[sum(geometry[16*y+x]<<x for x in range(16)) for y in range(16)])
            expected=words[4:]+middles[middle:middle+max(0,count-2)*2]+words[:2] if count>1 else words[4:]
            self.assertEqual(expected,case['route_words'])

    def test_strict_contract_authenticates_all_native_endpoint_and_route_words(self):
        manifest=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json').read_text())
        entry=next(e for e in manifest['entries'] if e['id']=='oracle-grid-passages-engine')
        keys=('fixture','size_class','mask','terrain_flags','start','goal','source_words','target_words','endpoints','result','route_words')
        rows=[{k:c[k] for k in keys} for c in self.fixture['cases']]
        digest=hashlib.sha256(json.dumps(rows,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        self.assertEqual(entry['checks']['passage_words_sha256']['equal'],digest)
        rows[0]['endpoints'][0]^=1
        changed=hashlib.sha256(json.dumps(rows,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        self.assertNotEqual(digest,changed)

    def test_matrix_covers_lanes_classes_offsets_and_geometry_families(self):
        cases=self.fixture['cases']
        self.assertEqual({c['mask'] for c in cases},{0x02000002,0x04000004,0x40000040,0x80000080})
        self.assertEqual({c['size_class'] for c in cases},{0,1,2,3})
        self.assertEqual(sum(all(c['endpoints']) for c in cases),2720)
        self.assertEqual(sum(c['result'] for c in cases),1664)
        shapes={p['shape'] for p in self.fixture['profiles'].values()}
        self.assertEqual(len(shapes),50)
        for prefix,count in [('vertical_',6),('horizontal_',6),('corner_',24),('touching_',6),('edge_',4),('lane_wall_',4)]:
            self.assertEqual(sum(s.startswith(prefix) for s in shapes),count)


if __name__=='__main__':unittest.main()
