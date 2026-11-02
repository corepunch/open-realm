"""Reject incomplete or state-changing native portal evidence."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.verify_warp165_live import transitions


class WarpMarkerEvidence(unittest.TestCase):
    def rows(self):
        rows = [dict(event='tick', group='g', identity=[1, 2],
                     members=[dict(path=f'p{i}') for i in range(4)])]
        for i in range(4):
            before = dict(path=f'p{i}', fineCount=10 + i, fineIndex=0,
                          accCount=5, accIndex=2, flags='3a30000',
                          dest=[1113144680, 1114453809], adjusted=[1113144680, 1114453809])
            after = dict(before, fineIndex=-1, accIndex=0)
            rows.append(dict(event='warp', result=1, caller='165c57', before=before, after=after))
            rows.append(dict(event='commit', group='g', path=f'p{i}', mflags='180000'))
        return rows

    def test_complete_transitions_retain_member_flags(self):
        self.assertEqual(transitions(self.rows()), 4)

    def test_count_flag_and_destination_replacement_is_rejected(self):
        for field, value in [('fineCount', 0), ('accCount', 0), ('flags', '2a30000'),
                             ('dest', [0, 0]), ('adjusted', [0, 0])]:
            rows = self.rows()
            rows[1]['after'][field] = value
            with self.assertRaisesRegex(ValueError, 'retained route'):
                transitions(rows)

    def test_missing_index_invalidation_is_rejected(self):
        for field, value in [('fineIndex', 0), ('accIndex', 2)]:
            rows = self.rows()
            rows[1]['after'][field] = value
            with self.assertRaisesRegex(ValueError, 'indices'):
                transitions(rows)

    def test_marker_loss_after_teleport_is_rejected(self):
        rows = self.rows()
        rows[2]['mflags'] = '100000'
        with self.assertRaisesRegex(ValueError, 'member marker'):
            transitions(rows)

    def test_unrelated_new_group_can_replace_the_same_path(self):
        rows = self.rows()
        rows.extend([dict(event='tick', group='g', identity=[1, 3], members=[dict(path='p0')]),
                     dict(event='commit', group='g', path='p0', mflags='100000')])
        self.assertEqual(transitions(rows), 4)

    def test_portal_does_not_refresh_formation(self):
        for event in ('layout', 'advance', 'regroup'):
            rows = self.rows() + [dict(event=event, group='g')]
            with self.assertRaisesRegex(ValueError, 'physical formation'):
                transitions(rows)

    def test_incomplete_and_unowned_warps_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            transitions(self.rows()[:-2])
        rows = self.rows()
        rows[1]['before']['path'] = rows[1]['after']['path'] = 'unknown'
        with self.assertRaisesRegex(ValueError, 'physical member owner'):
            transitions(rows)

    def test_frozen_observations_cover_three_repeats_and_one_control(self):
        frozen = json.loads((ROOT / 'tools/ghidra/fixtures/retail-warp-markers165-1.27.json').read_text())
        modes = [p['metadata']['mode'] for p in frozen['captures'].values()]
        self.assertEqual(modes.count('observe'), 3)
        self.assertEqual(modes.count('control'), 1)
        for pin in frozen['captures'].values():
            if pin['metadata']['mode'] != 'observe':
                continue
            self.assertEqual(len(pin['normalized']['warps']), 4)
            self.assertEqual([r[0] for r in pin['normalized']['groups'][0]['cooldown_seeds']], [3, 69, 135])


if __name__ == '__main__':
    unittest.main()
