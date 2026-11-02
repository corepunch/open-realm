import copy
import gzip
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida/research'))
import recovery197_verify as V


class BlockedRecovery(unittest.TestCase):
    def rows(self):
        fixture = json.loads(gzip.decompress((ROOT /
            'tools/ghidra/fixtures/retail-blocked-recovery197-1.27.json.gz').read_bytes()))
        stages = fixture['stages']
        return copy.deepcopy(stages['searches'] + stages['recovery'] + stages['successor'])

    def test_repeated_original_stages_are_complete(self):
        result = V.recovery(self.rows())
        self.assertEqual(len(result['recovery']), 67)
        self.assertEqual(len(result['searches']), 4)

    def test_rejects_notification_without_pending_head_activation(self):
        rows = self.rows()
        end = next(r for r in rows if r['event'] == 'recover-end')
        end['unit']['orders'] = [0xffffffff] * 2
        with self.assertRaises(ValueError):
            V.recovery(rows)

    def test_rejects_stale_retry_and_scheduler_state(self):
        for field, value in [('retry', [0, 1]), ('links', [1, 0]), ('counts', [1, 0])]:
            rows = self.rows()
            next(r for r in rows if r['event'] == 'stop-end')['path'][field] = value
            with self.assertRaises(ValueError):
                V.recovery(rows)

    def test_rejects_missing_cleanup_task(self):
        rows = self.rows()
        rows.pop(next(i for i, r in enumerate(rows) if r['event'] == 'prepend-event'))
        with self.assertRaises(ValueError):
            V.recovery(rows)

    def test_rejects_reference_leak(self):
        rows = self.rows()
        next(r for r in rows if r['event'] == 'recover-end')['unit']['refs'] += 1
        with self.assertRaises(ValueError):
            V.recovery(rows)

    def test_rejects_successor_that_keeps_internal_tasks(self):
        rows = self.rows()
        next(r for r in rows if r['event'] == 'arrival-cleanup-end' and r['c'] == 1376)['unit']['tasks'] = [1, 1]
        with self.assertRaises(ValueError):
            V.recovery(rows)


if __name__ == '__main__':
    unittest.main()
