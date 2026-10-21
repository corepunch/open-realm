"""Retain the native placement45 timing and independent occupant order."""
import base64
from collections import Counter
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ghidra/research'))
import entangle_retry_evidence as v


class EntangleRetryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def change(self, edit_rows=None, edit_preload=None, index=0):
        data = copy.deepcopy(self.bundle)
        capture = data['captures'][index]
        rows = [json.loads(line) for line in v.literal(capture, 'raw').splitlines()]
        if edit_preload:
            raw = edit_preload(v.literal(capture, 'preload').decode()).encode()
            capture.update(preload_b64=base64.b64encode(raw).decode(), preload_sha256=hashlib.sha256(raw).hexdigest())
            rows[-1]['sha256'] = capture['preload_sha256']
        if edit_rows:
            edit_rows(rows)
        for end in [row for row in rows if row.get('event') == 'trace-end']:
            end['counts'] = dict(Counter(row['event'] for row in rows if row['event'] in end['counts']))
        raw = b''.join((json.dumps(row) + '\n').encode() for row in rows)
        capture.update(raw_b64=base64.b64encode(raw).decode(), raw_sha256=hashlib.sha256(raw).hexdigest())
        return data

    def test_complete_native_retry_matrix(self):
        self.assertEqual(v.audit(), dict(observations=2, controls=1, public_markers=279, retries=10,
                                        engine_parity=False, remaining_owner='TARGET-01.2'))

    def test_ten_bounded_retries_required(self):
        def edit(rows):
            [row for row in rows if row.get('event') == 'root-retry-enter' and row['scene'] == 1][-1]['retries'] = 9
        with self.assertRaisesRegex(ValueError, 'bounded retry counter differs'):
            v.audit(self.change(edit_rows=edit))

    def test_half_second_delay_required(self):
        def edit(rows):
            target = [row for row in rows if row.get('event') == 'root-retry-enter' and row['scene'] == 1][1]
            target['clock']['primary'][0] = v.word(40)
        with self.assertRaisesRegex(ValueError, 'half-second retry delay differs'):
            v.audit(self.change(edit_rows=edit))

    def test_deferred_resume_required(self):
        def edit(rows):
            rows.pop(next(i for i, row in enumerate(rows) if row.get('event') == 'root-resume-enter' and row['scene'] == 1))
        with self.assertRaisesRegex(ValueError, 'deferred retry ownership differs'):
            v.audit(self.change(edit_rows=edit))

    def test_placement45_required(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'entangle-build-filter' and
                 row['scene'] == 1 and 0x438ac0 <= row['caller'] < 0x438d00)['result'] = 0
        with self.assertRaisesRegex(ValueError, 'retry placement45 differs'):
            v.audit(self.change(edit_rows=edit))

    def test_move_precedes_unroot(self):
        with self.assertRaisesRegex(ValueError, 'Move must precede Unroot'):
            v.audit(self.change(index=2, edit_preload=lambda s:
                s.replace('overlays=0 whead=851986 wx=1024.000', 'overlays=0 whead=0 wx=1024.000', 1)))

    def test_control_is_unhooked(self):
        with self.assertRaisesRegex(ValueError, 'control has observer hooks'):
            v.audit(self.change(index=2, edit_rows=lambda rows: rows.insert(1, {'event': 'root-retry-enter'})))

    def test_all_public_markers_compared(self):
        with self.assertRaisesRegex(ValueError, 'repeats/control differ'):
            v.audit(self.change(index=2, edit_preload=lambda s: s.replace(' wx=0.000', ' wx=0.001', 1)))
