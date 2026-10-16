"""Native denial records must preserve event order and generation boundaries."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.verify_target164_live import failure_visits


class TargetDelayEvidence(unittest.TestCase):
    def rows(self):
        return [dict(event='group-new', group='0x100', identity=[1, 2]),
                dict(event='route', group='0x100', visit=1, counter=1108, result=0, ready=1,
                     final=123456, dest=[0x42440000, 0x41f41d4a],
                     path=dict(path='0x200', accCount=0, accIndex=-1, accTime=0)),
                dict(event='stop-members', group='0x100', visit=1, rows=[[0, '0', 0, '0x300']]),
                dict(event='route', group='0x100', visit=2, result=1),
                dict(event='advance', group='0x100', visit=2, resetMembers=0, resetCounters=1),
                dict(event='layout', group='0x100', visit=2)]

    def test_unwritten_failure_result_is_not_compared(self):
        rows = self.rows()
        expected = failure_visits(rows)
        rows[1]['final'] = 0xdeadbeef
        self.assertEqual(failure_visits(rows), expected)
        self.assertEqual(expected[1], 1)

    def test_missing_or_duplicate_stop_is_rejected(self):
        for count in (0, 2):
            rows = self.rows()
            rows[2:3] = [copy.deepcopy(rows[2]) for _ in range(count)]
            with self.assertRaisesRegex(ValueError, 'stop exactly once'):
                failure_visits(rows)

    def test_regroup_layout_or_advance_on_failure_is_rejected(self):
        for event in ('regroup', 'layout', 'advance'):
            rows = self.rows()
            rows.append(dict(event=event, group='0x100', visit=1))
            with self.assertRaisesRegex(ValueError, 'without regroup'):
                failure_visits(rows)

    def test_nonzero_stop_speed_and_valid_denied_route_are_rejected(self):
        rows = self.rows()
        rows[2]['rows'][0][0] = 0x3f800000
        with self.assertRaisesRegex(ValueError, 'requested speed'):
            failure_visits(rows)
        for field, value in [('accCount', 1), ('accIndex', 0), ('accTime', 1108)]:
            rows = self.rows()
            rows[1]['path'][field] = value
            with self.assertRaisesRegex(ValueError, 'denied route state'):
                failure_visits(rows)

    def test_recovery_must_keep_members_and_rebuild_layout(self):
        rows = self.rows()
        rows[4]['resetMembers'] = 1
        with self.assertRaisesRegex(ValueError, 'retain members'):
            failure_visits(rows)
        rows = self.rows()[:-1]
        with self.assertRaisesRegex(ValueError, 'refresh layout'):
            failure_visits(rows)

    def test_reused_group_address_starts_an_independent_lifetime(self):
        rows = self.rows()
        replacement = copy.deepcopy(rows)
        replacement[0]['identity'] = [1, 3]
        failed, recovered = failure_visits(rows + replacement)
        self.assertEqual(len(failed), 2)
        self.assertNotEqual(failed[0][0], failed[1][0])
        self.assertEqual(recovered, 2)

    def test_exported_words_are_pinned_and_do_not_use_rounded_report_positions(self):
        frozen = json.loads((ROOT / 'tools/ghidra/fixtures/retail-target-delay164-1.27.json').read_text())
        pin = frozen['header']
        raw = (ROOT / pin['path']).read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), pin['sha256'])
        self.assertEqual(raw.count(b'    {0x'), 235)
        self.assertEqual(len(frozen['captures']), 5)
        self.assertEqual(len(frozen['controls']), 2)


if __name__ == '__main__':
    unittest.main()
