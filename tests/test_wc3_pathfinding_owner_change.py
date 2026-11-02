"""The frozen public owner transition rejects changed words, ordering and provenance."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_owner_change_trace import EVENTS, render_header, verify_lifecycle


class OwnerChangeTests(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-owner-change-1.27.json').read_text())
        self.rows = [dict(self.fixture['metadata'], event='metadata', pid=123), *copy.deepcopy(self.fixture['lifecycle'])]
        counts = {event: sum(row['event'] == event for row in self.rows) for event in EVENTS - {'marker'}}
        self.rows.append(dict(event='trace-end', installed=True, counts=counts))

    def test_complete_frozen_owner_journey(self):
        result = verify_lifecycle(self.rows, self.fixture)
        self.assertEqual(result['lifecycle_records'], 365)
        self.assertEqual(result['public_velocity_commits'], 17)
        self.assertEqual(result['ownership_stops'], 2)

    def test_raw_stop_motion_class_order_and_samples_cannot_change(self):
        for kind in ('stop', 'velocity', 'class', 'order', 'missing', 'sample', 'count', 'metadata', 'error', 'unfinished'):
            with self.subTest(kind=kind):
                rows = copy.deepcopy(self.rows)
                if kind == 'stop': next(r for r in rows if r['event'] == 'mover-stop' and '0x698d92' in r['stack'])['after'][2] ^= 1
                elif kind == 'velocity': next(r for r in rows if r['event'] == 'velocity-commit')['after'][4] ^= 1
                elif kind == 'class': next(r for r in rows if r['event'] == 'scheduler-class' and r['value'] == 1)['value'] = 2
                elif kind == 'order': rows[2], rows[3] = rows[3], rows[2]
                elif kind == 'missing': rows.pop(5)
                elif kind == 'sample': next(r for r in rows if r['event'] == 'marker' and 'after_owner_change' in r['value'])['value'] += ' changed'
                elif kind == 'count': rows[-1]['counts']['velocity-commit'] -= 1
                elif kind == 'metadata': rows[0]['owned'] = False
                elif kind == 'error': rows.append(dict(type='error'))
                else: rows[-1]['installed'] = False
                with self.assertRaises(ValueError): verify_lifecycle(rows, self.fixture)

    def test_engine_header_is_exactly_frozen(self):
        self.assertEqual((ROOT / 'games/warcraft-3/game/tests/retail_owner_change.h').read_text(), render_header(self.fixture))


if __name__ == '__main__':
    unittest.main()
