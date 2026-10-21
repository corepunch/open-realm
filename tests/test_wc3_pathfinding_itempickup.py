"""Reject weakened ordinary-pickup capture and public-outcome contracts."""
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
import itempickup_evidence as v


class ItemPickupEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def change(self, index=0, setting='stock', edit_rows=None, edit_preload=None):
        data = copy.deepcopy(self.bundle)
        capture = data['settings'][setting]['captures'][index]
        rows = [json.loads(line) for line in v.literal(capture, 'raw').splitlines()]
        if edit_preload:
            raw = edit_preload(v.literal(capture, 'preload').decode()).encode()
            capture.update(preload_b64=base64.b64encode(raw).decode(), preload_sha256=hashlib.sha256(raw).hexdigest())
            rows[-1]['sha256'] = capture['preload_sha256']
        if edit_rows:
            edit_rows(rows)
        ends = [row for row in rows if row.get('event') == 'trace-end']
        if ends:
            counts = ends[0]['counts']
            ends[0]['counts'] = dict(Counter(row['event'] for row in rows if row['event'] in counts))
        raw = b''.join((json.dumps(row) + '\n').encode() for row in rows)
        capture.update(raw_b64=base64.b64encode(raw).decode(), raw_sha256=hashlib.sha256(raw).hexdigest())
        return data

    def test_complete_native_matrix(self):
        self.assertEqual(v.audit(), dict(observations=6, controls=3, public_markers=2610,
                                        engine_parity=False, remaining_owner='TARGET-01.2'))

    def test_control_required(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['zero']['captures'].pop()
        with self.assertRaisesRegex(ValueError, 'control required'):
            v.audit(data)

    def test_literal_capture_bytes_required(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['stock']['captures'][0]['raw_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'literal capture bytes changed'):
            v.audit(data)

    def test_authored_map_bytes_required(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['authored']['map']['raw_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'literal capture bytes changed'):
            v.audit(data)

    def test_probe_sources_pinned(self):
        def edit(rows):
            rows[0]['source_sha256']['itemdrop_probe.j'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'pickup source changed'):
            v.audit(self.change(edit_rows=edit))

    def test_only_research_environments_allowed(self):
        with self.assertRaisesRegex(ValueError, 'pickup research environment'):
            v.audit(self.change(edit_rows=lambda rows: rows[0].update(env='A')))

    def test_target_range_words_required(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'target-task' and row['code'] == 0xd0171)['range'] = 0
        with self.assertRaisesRegex(ValueError, 'target task range differs'):
            v.audit(self.change(edit_rows=edit))

    def test_pause_reconstructs_both_tasks(self):
        def edit(rows):
            extra = [row for row in rows if row.get('event') == 'target-task' and row['scene'] == 9][-2:]
            rows[:] = [row for row in rows if not any(row is old for old in extra)]
        with self.assertRaisesRegex(ValueError, 'target task chain/resume differs'):
            v.audit(self.change(edit_rows=edit))

    def test_tasks_keep_same_item_identity(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'target-task')['target'] = '0x1'
        with self.assertRaisesRegex(ValueError, 'task target identity differs'):
            v.audit(self.change(edit_rows=edit))

    def test_control_has_no_observer(self):
        with self.assertRaisesRegex(ValueError, 'control has observer hooks'):
            v.audit(self.change(2, edit_rows=lambda rows: rows.insert(1, dict(event='pickup-action-enter'))))

    def test_zero_far_pickup_must_fail_radial_terminal(self):
        def edit(text):
            line = next(line for line in text.splitlines() if 'scene=1 label=settled ' in line)
            return text.replace(line, line.replace('anyowned=0', 'anyowned=1'))
        with self.assertRaisesRegex(ValueError, 'final item identity/ownership differs'):
            v.audit(self.change(2, setting='zero', edit_preload=edit))

    def test_hidden_zero_range_item_still_collects(self):
        def edit(text):
            line = next(line for line in text.splitlines() if 'scene=15 label=settled ' in line)
            return text.replace(line, line.replace('anyowned=1', 'anyowned=0'))
        with self.assertRaisesRegex(ValueError, 'final item identity/ownership differs'):
            v.audit(self.change(2, setting='zero', edit_preload=edit))

    def test_public_smart_head_required(self):
        with self.assertRaisesRegex(ValueError, 'public head differs'):
            v.audit(self.change(2, edit_preload=lambda s: s.replace('label=smart-return accepted=1 head=851971',
                                                                   'label=smart-return accepted=1 head=0', 1)))

    def test_every_public_marker_compared(self):
        with self.assertRaisesRegex(ValueError, 'pickup repeats/control differ'):
            v.audit(self.change(2, edit_preload=lambda s: s.replace('x=922.442', 'x=922.441', 1)))
