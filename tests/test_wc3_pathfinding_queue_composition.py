"""Natural full-search queue expectations are original outputs, including denied final work."""
import collections
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

    def test_denied_goal_pop_remains_a_partial_centre(self):
        self.assertEqual(self.fixture['budget_goal_input']['output'][:3],[0,701,669])
        self.assertEqual(self.fixture['budget_goal_route_words'][:2],[0x422e0000,0x422e0000])
        self.assertEqual(len(self.fixture['budget_goal_route_words']),100)

if __name__=='__main__':unittest.main()
