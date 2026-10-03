"""Natural full-search queue expectations are original outputs, including denied final work."""
import hashlib
import json
from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[1]

class QueueCompositionTests(unittest.TestCase):
    def setUp(self):
        self.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-fine-queue-1.27.json').read_text())
        self.source=(ROOT/'games/warcraft-3/game/tests/retail_fine_queue.h').read_text()

    def test_literal_input_maps_and_complete_routes_match_native(self):
        for name,key in [('retail_queue_rows','input'),('retail_budget_goal_rows','budget_goal_input')]:
            table=self.source.split(name+'[]={',1)[1].split('};',1)[0]
            masks=[int(v,16) for v in re.findall(r'0x([0-9a-f]+)ULL',table)]
            actual=bytes(2 if masks[y]&(1<<x) else 0 for y in range(48) for x in range(48))
            self.assertEqual(actual,bytes.fromhex(self.fixture[key]['cells']))
        for name,key in [('retail_queue_route','route_words'),('retail_budget_goal_route','budget_goal_route_words')]:
            table=self.source.split(name+'[][2]={',1)[1].split('};',1)[0]
            self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',table)],self.fixture[key])

    def test_every_queue_word_and_natural_reopening_is_preserved(self):
        table=self.source.split('retail_queue_pops[][10]={',1)[1].split('};',1)[0]
        words=[int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',table)]
        self.assertEqual(words,[v for r in self.fixture['pops'] for v in r])
        rows=self.fixture['pops']; self.assertEqual(len(rows),1068)
        self.assertEqual(sum(r[2]!=r[3] for r in rows),190)
        valid=[r for r in rows if r[2]==r[3]]
        self.assertEqual(len(valid)-len({r[1] for r in valid}),1)
        self.assertEqual(len(rows)-len({r[0] for r in rows}),886)
        self.assertEqual([r[4] for r in rows],list(range(1,1069)))

    def test_stamp_wrap_nodes_and_routes_are_frozen_native_clean_controls(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-fine-stamp-wrap-1.27.json').read_text())
        self.assertEqual(fixture['terrain_sha256'],hashlib.sha256(bytes.fromhex(self.fixture['budget_goal_input']['cells'])).hexdigest())
        self.assertEqual([r['stamp'] for r in fixture['cases']],[65535,0,1,2])
        for row in fixture['cases']:
            name='ground' if row['cls']==0 else 'other'
            for suffix,key,width in [('nodes','node_state',7),('route','route_words',2)]:
                table=self.source.split('retail_wrap_'+name+'_'+suffix+'[]['+str(width)+']={',1)[1].split('};',1)[0]
                actual=[int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',table)]
                expected=[v for n in row[key] for v in n] if key=='node_state' else row[key]
                self.assertEqual(actual,expected)
            self.assertEqual(len(row['node_state']),row['nodes'])

    def test_adaptive_wrap_fixture_preserves_every_original_node_and_route(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-adaptive-stamp-wrap-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/retail_adaptive_wrap.h').read_text()
        table=source.split('retail_adaptive_wrap_rows[][32]={',1)[1].split('};',1)[0]
        self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',table)],[v for row in fixture['input_rows'] for v in row])
        self.assertEqual([r['stamp'] for r in fixture['cases']],[0xfffffffe,0xffffffff,0,1,2,3,4,5])
        for i,row in enumerate(fixture['cases']):
            for prefix,key,width in [('nodes','node_state',8),('route','route_words',2)]:
                table=source.split('retail_adaptive_wrap_'+prefix+'_'+str(i)+'[]['+str(width)+']={',1)[1].split('};',1)[0]
                expected=[v for n in row[key] for v in n] if key=='node_state' else row[key]
                self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',table)],expected)
            self.assertEqual(len(row['node_state']),row['nodes'])
        self.assertEqual(sum(r['result']==0 for r in fixture['cases']),2)

    def test_denied_goal_pop_remains_a_partial_centre(self):
        self.assertEqual(self.fixture['budget_goal_input']['output'][:3],[0,701,669])
        self.assertEqual(self.fixture['budget_goal_route_words'][:2],[0x422e0000,0x422e0000])
        self.assertEqual(len(self.fixture['budget_goal_route_words']),100)

    def test_terrain_producer_inventory_and_engine_passage_preserve_native_words(self):
        fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-adaptive-terrain-producer-1.27.json').read_text())
        source=(ROOT/'games/warcraft-3/game/tests/retail_adaptive_producer.h').read_text()
        for name,width,expected in [
            ('inventory',8,[v for r in fixture['inventory'] for v in r['fine_flags']+r['classes']]),
            ('nodes',8,[v for r in fixture['searches'][0]['node_state'] for v in r]),
            ('route',2,fixture['searches'][0]['route_words'])]:
            table=source.split('retail_producer_'+name+'[]['+str(width)+']={',1)[1].split('};',1)[0]
            self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',table)],expected)
        table=source.split('retail_producer_classes[]={',1)[1].split('};',1)[0]
        self.assertEqual([int(v,16) for v in re.findall(r'0x([0-9a-f]+)',table)],fixture['class_bytes'])
        table=source.split('retail_producer_rows[]={',1)[1].split('};',1)[0]
        rows=[int(v,16) for v in re.findall(r'0x([0-9a-f]+)u',table)]
        self.assertEqual([0xc6 if rows[y//2]&(1<<(x//2)) else 0 for y in range(64) for x in range(64)],fixture['fine_flags'])
        self.assertEqual(len(fixture['inventory']),54)
        self.assertEqual(len(fixture['rejected_tuples']),27)
        self.assertEqual(fixture['hierarchy_sides'],[41,20,10,5])
        for row in fixture['searches']:
            self.assertEqual((row['result'],row['pops'],row['nodes']),(0,38,56))
            self.assertEqual(row['node_state'],fixture['searches'][0]['node_state'])
            self.assertEqual(row['route_words'],fixture['searches'][0]['route_words'])
            self.assertEqual(row['east_boundary_checks'][0]['original'],0)
            self.assertEqual(row['east_ordinary_occupancy'],1)

if __name__=='__main__':unittest.main()
