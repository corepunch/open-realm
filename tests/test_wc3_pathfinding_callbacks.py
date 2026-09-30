"""Frozen original callback mutations retain ordering and complete member words."""
import hashlib
import itertools
import json
from pathlib import Path
import unittest

FIXTURES=Path(__file__).resolve().parents[1]/'tools/ghidra/fixtures'


class CallbackFixtures(unittest.TestCase):
    def test_every_callback_position_and_removal_subset_is_recorded(self):
        fixture=json.loads((FIXTURES/'retail-callback-mutations-1.27.json').read_text())
        cases=fixture['cases']
        expected={(count,trigger,removed,action) for count in range(1,4)
                  for trigger,removed,action in itertools.product(range(count),range(1<<count),
                                                                  ['unbind_member','detach_mover'])}
        observed={(c['count'],c['trigger'],c['removed'],c['action']) for c in cases}
        self.assertEqual(observed,expected)
        self.assertEqual(len(cases),len(expected))
        digest=hashlib.sha256(json.dumps(cases,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        self.assertEqual(digest,fixture['cases_sha256'])
        self.assertIn('handle reclaim/reuse excluded',fixture['scope'])

    def test_post_callback_resolution_preserves_all_surviving_words(self):
        cases=json.loads((FIXTURES/'retail-callback-mutations-1.27.json').read_text())['cases']
        for case in cases:
            count,trigger,removed=(case[k] for k in ('count','trigger','removed'))
            # Higher rows already ran; lower rows must re-resolve after mutation.
            callbacks=list(reversed(range(trigger,count)))
            callbacks.extend(n for n in reversed(range(trigger)) if not removed & (1<<n))
            self.assertEqual(case['callback_order'],callbacks)
            survivors=list(range(count))
            for index in reversed(range(count)):
                if removed & (1<<index):
                    survivors[index]=survivors[-1]
                    survivors.pop()
            self.assertEqual(case['later_callback_order'],list(reversed(survivors)))
            words=[]
            for actor in survivors:
                row=[actor,100+actor]+[0x24680000+16*actor+k for k in range(2,11)]
                row[5]='mover'+str(actor)
                words.append(row)
            self.assertEqual(case['surviving_rows'],words)
        swapped=next(c for c in cases if (c['count'],c['trigger'],c['removed'],c['action'])==
                     (3,2,1,'detach_mover'))
        self.assertEqual([r[0] for r in swapped['surviving_rows']],[2,1])
        self.assertEqual(swapped['callback_order'],[2,1])
        self.assertEqual(swapped['later_callback_order'],[1,2])

    def test_persisted_member_layout_decodes_original_pair_requests(self):
        schema=json.loads((FIXTURES/'retail-pathfinding-types-1.27.json').read_text())
        types={t['name']:t for t in schema['layouts']}
        member=types['WC3PathMember']
        offsets={f['name']:f['offset'] for f in member['fields']}
        self.assertEqual(member['length'],44)
        self.assertNotIn(8,offsets.values())  # Unrecovered word stays undefined.
        pair=json.loads((FIXTURES/'retail-shared-pair-1.27.json').read_text())
        rows=pair['output']['normalized_states'][0]['group']['members']
        requests=[]
        for row in rows:
            requests.append({name:row[offsets[name]//4] for name in ('resolved','speed','heading','flags')})
        self.assertEqual(requests,[dict(resolved='mover',speed=0x41000000,heading=0xbe7aeac0,flags=0x100000),
                                   dict(resolved='second_mover',speed=0x41000000,heading=0xbe7aeac0,flags=0x100000)])
        group={f['name']:f['offset'] for f in types['WC3PathGroupPrefix']['fields']}
        vector={f['name']:f['offset'] for f in types['WC3PathMembersPrefix']['fields']}
        self.assertEqual(group['members']+vector['data'],0x28)
        self.assertEqual(group['members']+vector['count'],0x38)


if __name__=='__main__':unittest.main()
