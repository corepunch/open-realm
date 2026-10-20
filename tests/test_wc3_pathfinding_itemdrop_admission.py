"""Reject weak item-drop admission evidence without claiming engine closure."""
import base64
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ghidra/research'))
import itemdrop_evidence as v


class ItemDropAdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.events = json.loads(gzip.decompress(v.ADMISSION_BUNDLE.read_bytes()))
        cls.immobile = json.loads(gzip.decompress(v.IMMOBILE_BUNDLE.read_bytes()))

    def changed(self, *, index=2, immobile=False, rows_edit=None, preload_edit=None):
        data = copy.deepcopy(self.immobile if immobile else self.events)
        capture = data['captures'][index]
        rows = [json.loads(line) for line in base64.b64decode(capture['raw_b64']).splitlines()]
        if rows_edit:
            rows_edit(rows)
        if preload_edit:
            raw = preload_edit(base64.b64decode(capture['preload_b64']).decode()).encode()
            capture.update(preload_b64=base64.b64encode(raw).decode(), preload_sha256=hashlib.sha256(raw).hexdigest())
            rows[-1]['sha256'] = capture['preload_sha256']
        raw = b''.join((json.dumps(row) + '\n').encode() for row in rows)
        capture.update(raw_b64=base64.b64encode(raw).decode(), raw_sha256=hashlib.sha256(raw).hexdigest())
        return data

    def test_both_literal_repeat_control_contracts(self):
        want = dict(observations=2, controls=1, public_markers=819, positive_event_kinds=3,
                    drop_issued_events=0, paused_drop_accepted=False, engine_parity=False)
        self.assertEqual(v.audit_admission(), want)
        self.assertEqual(v.audit_admission(immobile=True), want)

    def test_unhooked_control_required(self):
        data = copy.deepcopy(self.events)
        data['captures'].pop()
        with self.assertRaisesRegex(ValueError, 'unhooked control required'):
            v.audit_admission(data)

    def test_target_event_positive_control_required(self):
        data = self.changed(preload_edit=lambda text: text.replace('label=event kind=target', 'label=event kind=point'))
        with self.assertRaisesRegex(ValueError, 'positive controls differ'):
            v.audit_admission(data)

    def test_specialized_drop_does_not_fire_ordinary_point_event(self):
        data = self.changed(preload_edit=lambda text: text.replace('kind=point order=851986', 'kind=point order=852001'))
        with self.assertRaisesRegex(ValueError, 'positive controls differ'):
            v.audit_admission(data)

    def test_paused_drop_is_rejected(self):
        data = self.changed(preload_edit=lambda text: text.replace('tick=202 scene=10 label=drop-return accepted=0',
                                                                  'tick=202 scene=10 label=drop-return accepted=1'))
        with self.assertRaisesRegex(ValueError, 'silent issued frontier differs'):
            v.audit_admission(data)

    def test_paused_rejection_preserves_suspension_head(self):
        data = self.changed(preload_edit=lambda text: text.replace('tick=202 scene=10 label=drop-return accepted=0 head=851973',
                                                                  'tick=202 scene=10 label=drop-return accepted=0 head=0'))
        with self.assertRaisesRegex(ValueError, 'current head differs'):
            v.audit_admission(data)

    def test_immobile_far_point_retains_item(self):
        def edit(text):
            return text.replace('tick=142 scene=7 label=drop-return accepted=1 head=0 type=1751674741 x=704.000 y=1024.000 owned=1',
                                'tick=142 scene=7 label=drop-return accepted=1 head=0 type=1751674741 x=704.000 y=1024.000 owned=0')
        with self.assertRaisesRegex(ValueError, 'immobile synchronous'):
            v.audit_admission(self.changed(immobile=True, preload_edit=edit), immobile=True)

    def test_immobile_completes_synchronously(self):
        data = self.changed(immobile=True, preload_edit=lambda text: text.replace('tick=122 scene=6 label=drop-return accepted=1 head=0',
                                                                                 'tick=122 scene=6 label=drop-return accepted=1 head=852001'))
        with self.assertRaisesRegex(ValueError, 'current head differs'):
            v.audit_admission(data, immobile=True)

    def test_original_class_instruction_guards(self):
        data = copy.deepcopy(self.events)
        data['instruction_guards']['0x684ab0'] = '90' * 6
        with self.assertRaisesRegex(ValueError, 'original admission code changed'):
            v.audit_admission(data)

    def test_captured_sources_cannot_be_rewritten(self):
        def edit(rows):
            rows[0]['source_sha256']['itemdrop_probe.j'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'source pins changed'):
            v.audit_admission(self.changed(index=0, rows_edit=edit))

    def test_literal_bytes_are_required(self):
        data = copy.deepcopy(self.events)
        data['captures'][0]['raw_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'literal capture bytes changed'):
            v.audit_admission(data)

    def test_control_has_no_observer(self):
        data = self.changed(rows_edit=lambda rows: rows.insert(1, dict(event='trace-end', installed=True)))
        with self.assertRaisesRegex(ValueError, 'control contains observer hooks'):
            v.audit_admission(data)

    def test_physical_task_range_and_association_are_retained(self):
        def edit(rows):
            next(r for r in rows if r.get('event') == 'point-task' and r['caller'] == 5567059)['association'] = '0x1'
        with self.assertRaisesRegex(ValueError, 'physical task differs'):
            v.audit_admission(self.changed(index=0, rows_edit=edit))

    def test_owned_research_environment_required(self):
        with self.assertRaisesRegex(ValueError, 'research environment differs'):
            v.audit_admission(self.changed(rows_edit=lambda rows: rows[0].update(env='A')))

    def test_every_public_marker_is_compared(self):
        data = self.changed(preload_edit=lambda text: text.replace('tick=0 scene=0 label=sample accepted=1 head=0 type=1215324524 x=704.000',
                                                                  'tick=0 scene=0 label=sample accepted=1 head=0 type=1215324524 x=705.000'))
        with self.assertRaisesRegex(ValueError, 'repeat/control markers differ'):
            v.audit_admission(data)

    def test_native_paused_return_matches_public_probe(self):
        def edit(rows):
            next(r for r in rows if r.get('event') == 'public-drop-point-return' and r['scene'] == 10)['result'] = 1
        with self.assertRaisesRegex(ValueError, 'original admission returns changed'):
            v.audit_admission(self.changed(index=0, rows_edit=edit))

    def test_original_map_is_preserved(self):
        data = copy.deepcopy(self.events)
        data['map']['raw_b64'] = base64.b64encode(b'wrong authored map').decode()
        with self.assertRaisesRegex(ValueError, 'map bytes differ'):
            v.audit_admission(data)
