"""Complete blocker scenes require trustworthy observation and control boundaries."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/ghidra'))
from research.verify_composed_blocker163_live import validate_rows


class ComposedBlockerEvidence(unittest.TestCase):
    def setUp(self):
        self.frozen = json.loads((ROOT/'tools/ghidra/fixtures/retail-composed-blocker163-1.27.json').read_text())
        self.binary = self.frozen['binary_sha256']
        self.spec = dict(mode='observe', metadata=dict(task='ROUTE-03.1', mode='observe',
                                                     sha256=self.binary, owned=True))

    def observed(self):
        return [dict(event='metadata', **self.spec['metadata']),
                dict(event='marker', value='ROUTE03 complete'),
                dict(event='trace-end', installed=True, finished=True),
                dict(event='preload-file', complete=True, markers=1)]

    def test_completed_observation_is_accepted(self):
        self.assertEqual(validate_rows(self.observed(), self.spec, self.binary)['markers'], 1)

    def test_wrong_binary_task_mode_or_unowned_process_is_rejected(self):
        for field, value in [('sha256', 'wrong'), ('task', 'different'), ('mode', 'control'), ('owned', False)]:
            rows = self.observed(); rows[0][field] = value
            with self.assertRaisesRegex(ValueError, 'provenance'):
                validate_rows(rows, self.spec, self.binary)

    def test_duplicate_or_late_metadata_is_rejected(self):
        for rows in [self.observed()[1:] + self.observed()[:1],
                     self.observed()[:1] + self.observed()]:
            with self.assertRaisesRegex(ValueError, 'one leading'):
                validate_rows(rows, self.spec, self.binary)

    def test_failed_hooks_and_incomplete_scene_are_rejected(self):
        rows = self.observed(); rows.insert(1, dict(type='error', description='hook failed'))
        with self.assertRaisesRegex(ValueError, 'failed capture'):
            validate_rows(rows, self.spec, self.binary)
        rows = self.observed(); rows[-1]['complete'] = False
        with self.assertRaisesRegex(ValueError, 'incomplete public'):
            validate_rows(rows, self.spec, self.binary)

    def test_late_unfinished_or_duplicate_observer_is_rejected(self):
        for field in ('installed', 'finished'):
            rows = self.observed(); rows[-2][field] = False
            with self.assertRaisesRegex(ValueError, 'incomplete observer'):
                validate_rows(rows, self.spec, self.binary)
        rows = self.observed(); rows.insert(1, copy.deepcopy(rows[-2]))
        with self.assertRaisesRegex(ValueError, 'incomplete observer'):
            validate_rows(rows, self.spec, self.binary)

    def test_missing_public_marker_is_rejected(self):
        rows = self.observed(); rows.pop(1)
        with self.assertRaisesRegex(ValueError, 'marker count'):
            validate_rows(rows, self.spec, self.binary)

    def test_control_has_no_injected_observation(self):
        spec = copy.deepcopy(self.spec); spec['mode'] = spec['metadata']['mode'] = 'control'
        rows = self.observed(); rows[0]['mode'] = 'control'
        with self.assertRaisesRegex(ValueError, 'instrumented control'):
            validate_rows(rows, spec, self.binary)
        validate_rows([rows[0], rows[-1]], spec, self.binary)

    def test_headers_are_pinned_to_original_stream_exports(self):
        self.assertEqual(len(self.frozen['headers']), 2)
        for name, pin in self.frozen['headers'].items():
            self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(), pin)
        self.assertEqual(sum(s['mode'] == 'observe' for s in self.frozen['captures'].values()), 19)
        self.assertEqual(len(self.frozen['controls']), 7)
        self.assertEqual(len(self.frozen['repeats']), 5)


if __name__ == '__main__':
    unittest.main()
