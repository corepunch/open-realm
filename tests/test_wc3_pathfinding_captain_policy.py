"""Reject incomplete and changed Captain policy evidence independently of hashes."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.verify_captain_policy159_live import DLL, check_rows


class CaptainPolicyEvidence(unittest.TestCase):
    def fixture(self):
        frozen = json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-policy159-1.27.json').read_text())
        rows = [dict(event='metadata', task='payoff159', mode='observe', owned=True,
                     sha256=DLL, source_sha256=frozen['observe_sources'])]
        for item in copy.deepcopy(frozen['normalized']):
            event = item['event']
            if event == 'speed':
                item['event'] = 'captain-speed'
                item['captain'] = 'owner%d' % item.pop('owner')
            elif event == 'update':
                owner = 'owner%d' % item.pop('owner')
                item['event'], item['name'] = 'captain-call', 'periodic-update'
                item['before']['captain'] = item['after']['captain'] = owner
            rows.append(item)
        rows += [dict(event='marker', value=m) for m in frozen['public_markers']]
        rows += [dict(event='trace-end', installed=True), dict(event='preload-file', markers=373, complete=True)]
        return rows, frozen

    def test_complete_policy_is_accepted(self):
        rows, frozen = self.fixture()
        check_rows(rows, 'observe', frozen)

    def test_changed_retreat_speed_is_rejected(self):
        rows, frozen = self.fixture()
        next(r for r in rows if r.get('event') == 'captain-speed' and r['word'] == 0x43fa0000)['word'] = 0x43870000
        with self.assertRaisesRegex(ValueError, 'policy differs'):
            check_rows(rows, 'observe', frozen)

    def test_missing_period_is_rejected(self):
        rows, frozen = self.fixture()
        del rows[next(i for i, r in enumerate(rows) if r.get('name') == 'periodic-update')]
        with self.assertRaisesRegex(ValueError, 'missing periodic'):
            check_rows(rows, 'observe', frozen)

    def test_changed_deadline_is_rejected(self):
        rows, frozen = self.fixture()
        next(r for r in rows if r.get('name') == 'periodic-update')['counter'] += 1
        with self.assertRaisesRegex(ValueError, 'policy differs'):
            check_rows(rows, 'observe', frozen)

    def test_changed_policy_inverse_is_rejected(self):
        rows, frozen = self.fixture()
        next(r for r in rows if r.get('name') == 'periodic-update')['after']['flags'] ^= 2
        with self.assertRaisesRegex(ValueError, 'policy differs'):
            check_rows(rows, 'observe', frozen)

    def test_control_cannot_contain_observer(self):
        rows, frozen = self.fixture()
        rows[0].update(task='GROUP-03.4.7.3', mode='control', source_sha256=frozen['control_sources'])
        with self.assertRaisesRegex(ValueError, 'control contains'):
            check_rows(rows, 'control', frozen)


if __name__ == '__main__':
    unittest.main()
