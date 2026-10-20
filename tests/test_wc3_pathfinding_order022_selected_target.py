"""Reject weakened selected-target evidence without claiming trajectory parity."""
import base64
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import audit_wc3_pathing_order022_selected_target as v


class SelectedTargetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def change(self, index, edit_rows=None, edit_preload=None):
        data = copy.deepcopy(self.bundle)
        item = data['captures'][index]
        rows = [json.loads(line) for line in base64.b64decode(item['raw_b64']).splitlines()]
        if edit_rows:
            edit_rows(rows)
        if edit_preload:
            raw = edit_preload(base64.b64decode(item['preload_b64']).decode()).encode()
            item.update(preload_b64=base64.b64encode(raw).decode(), preload_sha256=hashlib.sha256(raw).hexdigest())
            rows[-1]['sha256'] = item['preload_sha256']
        raw = b''.join((json.dumps(row) + '\n').encode() for row in rows)
        item.update(raw_b64=base64.b64encode(raw).decode(), raw_sha256=hashlib.sha256(raw).hexdigest())
        return data

    def test_complete_native_contract(self):
        result = v.audit()
        self.assertEqual((result['captures'], result['controls'], result['target_events']), (2, 1, 12))
        self.assertEqual(result['notification_contract_markers'], 30)
        self.assertFalse(result['engine_parity'])
        self.assertFalse(result['trajectory_parity'])

    def test_missing_repeat(self):
        data = copy.deepcopy(self.bundle)
        data['captures'].pop(1)
        with self.assertRaisesRegex(ValueError, 'repeats'):
            v.audit(data)

    def test_peer_cannot_bind_replacement_target(self):
        data = self.change(2, edit_preload=lambda s: s.replace('unit=3 id=851971 target=2', 'unit=3 id=851971 target=4'))
        with self.assertRaisesRegex(ValueError, 'original-target notification'):
            v.audit(data)

    def test_first_head_must_survive_through_peer_notification(self):
        data = self.change(2, edit_preload=lambda s: s.replace('target=2 c=851971 p=851971', 'target=2 c=0 p=851971'))
        with self.assertRaisesRegex(ValueError, 'original-target notification'):
            v.audit(data)

    def test_peer_cannot_publish_point_instead(self):
        data = self.change(2, edit_preload=lambda s: s.replace('issued-target unit=3', 'issued-point unit=3', 1))
        with self.assertRaisesRegex(ValueError, 'original-target notification'):
            v.audit(data)

    def test_removal_callback_required(self):
        data = self.change(2, edit_preload=lambda s: s.replace('label=target-replaced', 'label=no-removal', 1))
        with self.assertRaisesRegex(ValueError, 'original-target notification'):
            v.audit(data)

    def test_selected_recipient_packet_cannot_change_canonical_target(self):
        def edit(rows):
            packets = [row for row in rows if row.get('event') == 'net-order']
            packets[1]['order']['target'][1] += 1
        with self.assertRaisesRegex(ValueError, 'canonical target'):
            v.audit(self.change(0, edit_rows=edit))

    def test_selected_recipient_cannot_change_issue_point(self):
        def edit(rows):
            events = [row for row in rows if row.get('event') == 'target-event']
            events[1]['order']['point'][0] += 1
        with self.assertRaisesRegex(ValueError, 'canonical target or point'):
            v.audit(self.change(0, edit_rows=edit))

    def test_event_owner_must_have_new_head(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'target-event')['state']['head_order']['command'] = 0
        with self.assertRaisesRegex(ValueError, 'target-event owner'):
            v.audit(self.change(1, edit_rows=edit))

    def test_real_right_click_and_attack_input_required(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'player-input')['rc'] = 1
        with self.assertRaisesRegex(ValueError, 'owned target input'):
            v.audit(self.change(2, edit_rows=edit))

    def test_owned_window_required(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'player-input')['cmd'][0] = 'other-pid'
        with self.assertRaisesRegex(ValueError, 'owned target input'):
            v.audit(self.change(2, edit_rows=edit))

    def test_control_has_no_hooks(self):
        with self.assertRaisesRegex(ValueError, 'control has observer'):
            v.audit(self.change(2, edit_rows=lambda rows: rows.insert(1, dict(event='module', seq=1))))

    def test_literal_bytes_not_only_decoded_contract(self):
        data = copy.deepcopy(self.bundle)
        data['captures'][0]['raw_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'literal capture bytes'):
            v.audit(data)

    def test_source_provenance_required(self):
        def edit(rows):
            rows[0]['source_sha256']['order022_selected_target_probe.j'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'changed source'):
            v.audit(self.change(1, edit_rows=edit))

    def test_complete_probe_required(self):
        data = self.change(2, edit_preload=lambda s: s.replace('label=complete', 'label=incomplete'))
        with self.assertRaisesRegex(ValueError, 'probe never finished'):
            v.audit(data)

    def test_broken_callback_stream_rejected(self):
        def edit(rows):
            next(row for row in rows if row.get('seq') == 3)['seq'] = 99
        with self.assertRaisesRegex(ValueError, 'observer sequence'):
            v.audit(self.change(0, edit_rows=edit))

    def test_trajectory_is_preserved_but_not_compared(self):
        # A changed position has no bearing on this bounded notification claim.
        # The actual artifact's hash still preserves its original sample bytes.
        data = self.change(2, edit_preload=lambda s: s.replace('cx=512.000', 'cx=513.000', 1))
        self.assertFalse(v.audit(data)['trajectory_parity'])
