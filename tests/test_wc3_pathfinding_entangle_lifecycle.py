"""Preserve native Entangle interruption and occupant counterexamples."""
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
import entangle_lifecycle_evidence as v


class EntangleLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def change(self, setting='occupants', index=0, edit_rows=None, edit_preload=None):
        data = copy.deepcopy(self.bundle)
        capture = data['settings'][setting]['captures'][index]
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

    def test_complete_native_matrix(self):
        self.assertEqual(v.audit(), dict(observations=4, controls=2, public_markers=1806,
                                        engine_parity=False, remaining_owner='TARGET-01.2'))

    def test_control_required(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['occupants']['captures'].pop()
        with self.assertRaisesRegex(ValueError, 'control required'):
            v.audit(data)

    def test_literal_map_required(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['occupants']['map']['raw_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'literal capture bytes changed'):
            v.audit(data)

    def test_exact_sources_required(self):
        with self.assertRaisesRegex(ValueError, 'lifecycle source changed'):
            v.audit(self.change(edit_rows=lambda rows: rows[0]['source_sha256'].update({'itemdrop_probe.j': '0' * 64})))

    def test_environment_required(self):
        with self.assertRaisesRegex(ValueError, 'research environment'):
            v.audit(self.change(edit_rows=lambda rows: rows[0].update(env='A')))

    def test_unhooked_control_required(self):
        with self.assertRaisesRegex(ValueError, 'control has observer hooks'):
            v.audit(self.change(index=2, edit_rows=lambda rows: rows.insert(1, {'event': 'point-task'})))

    def test_hostile_occupant_site_required(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'entangle-placement' and row['scene'] == 2)['point'][0] = v.word(1152)
        with self.assertRaisesRegex(ValueError, 'retained placement differs'):
            v.audit(self.change(edit_rows=edit))

    def test_zero_physical_range_required(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'point-task' and row['code'] == 0xd018f)['range'] = v.word(333)
        with self.assertRaisesRegex(ValueError, 'Root continuation differs'):
            v.audit(self.change(edit_rows=edit))

    def test_resume_reconstruction_required(self):
        def edit(rows):
            target = [row for row in rows if row.get('event') == 'entangle-placement' and row['scene'] == 3][-1]
            target['point'][1] = v.word(960)
        with self.assertRaisesRegex(ValueError, 'retained placement differs'):
            v.audit(self.change(setting='interruptions', edit_rows=edit))

    def test_target_loss_cannot_cancel_admitted_point(self):
        with self.assertRaisesRegex(ValueError, 'cancelled approach early'):
            v.audit(self.change(setting='interruptions', index=2, edit_preload=lambda s:
                s.replace('tick=4340 scene=4 label=sample accepted=1 head=852147',
                          'tick=4340 scene=4 label=sample accepted=1 head=0')))

    def test_complete_stream_required(self):
        with self.assertRaisesRegex(ValueError, 'incomplete lifecycle public stream'):
            v.audit(self.change(edit_preload=lambda s: s.replace('label=complete', 'label=incomplete')))

    def test_every_public_marker_compared(self):
        with self.assertRaisesRegex(ValueError, 'repeats/control differ'):
            v.audit(self.change(index=2, edit_preload=lambda s: s.replace(' wx=0.000', ' wx=0.001', 1)))
