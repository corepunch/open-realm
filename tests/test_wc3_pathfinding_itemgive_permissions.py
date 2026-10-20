"""Preserve the literal permission matrix without claiming full TARGET closure."""
import base64
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ghidra/research'))
import itemgive_permissions_evidence as v


class ItemGivePermissionEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def change(self, index, edit_rows=None, edit_preload=None, variant='no-get'):
        data = copy.deepcopy(self.bundle)
        capture = data['settings'][variant]['captures'][index]
        rows = [json.loads(line) for line in v.literal(capture, 'raw').splitlines()]
        if edit_preload:
            raw = edit_preload(v.literal(capture, 'preload').decode()).encode()
            capture.update(preload_b64=base64.b64encode(raw).decode(), preload_sha256=hashlib.sha256(raw).hexdigest())
            rows[-1]['sha256'] = capture['preload_sha256']
        if edit_rows:
            edit_rows(rows)
        raw = b''.join((json.dumps(row) + '\n').encode() for row in rows)
        capture.update(raw_b64=base64.b64encode(raw).decode(), raw_sha256=hashlib.sha256(raw).hexdigest())
        return data

    def test_complete_permission_matrix(self):
        self.assertEqual(v.audit(), dict(observations=4, controls=2, public_markers=474,
                                        engine_parity=False, remaining_owner='TARGET-01.2'))

    def test_control_required(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['no-get']['captures'].pop()
        with self.assertRaisesRegex(ValueError, 'unhooked control required'):
            v.audit(data)

    def test_control_has_no_hooks(self):
        data = self.change(2, edit_rows=lambda rows: rows.insert(1, dict(event='can-get-items', value=0)))
        with self.assertRaisesRegex(ValueError, 'control contains observer hooks'):
            v.audit(data)

    def test_literal_capture_bytes(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['no-get']['captures'][0]['raw_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'literal capture bytes changed'):
            v.audit(data)

    def test_map_bytes(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['no-get']['map']['raw_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'literal capture bytes changed'):
            v.audit(data)

    def test_setting_cannot_claim_permission_enabled(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['no-get']['metadata']['can_get_items'] = 1
        with self.assertRaisesRegex(ValueError, 'authored permission map changed'):
            v.audit(data)

    def test_native_getter_required(self):
        data = self.change(0, edit_rows=lambda rows: rows.__setitem__(slice(None),
                           [row for row in rows if row.get('event') != 'can-get-items']))
        with self.assertRaisesRegex(ValueError, 'native pickup permission getter differs'):
            v.audit(data)

    def test_getter_must_confirm_actual_zero(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'can-get-items')['value'] = 1
        with self.assertRaisesRegex(ValueError, 'native pickup permission getter differs'):
            v.audit(self.change(0, edit_rows=edit))

    def test_forced_give_cannot_be_rejected(self):
        with self.assertRaisesRegex(ValueError, 'producer permission admission differs'):
            v.audit(self.change(2, edit_preload=lambda s: s.replace('label=give-return accepted=1',
                                                                   'label=give-return accepted=0')))

    def test_scripted_insertion_cannot_be_rejected(self):
        with self.assertRaisesRegex(ValueError, 'producer permission admission differs'):
            v.audit(self.change(2, edit_preload=lambda s: s.replace('label=script-return accepted=1',
                                                                   'label=script-return accepted=0')))

    def test_disabled_smart_cannot_be_accepted(self):
        with self.assertRaisesRegex(ValueError, 'producer permission admission differs'):
            v.audit(self.change(2, edit_preload=lambda s: s.replace('label=smart-return accepted=0',
                                                                   'label=smart-return accepted=1')))

    def test_synchronous_receipt_required(self):
        def edit(text):
            line = next(line for line in text.splitlines() if 'label=give-return ' in line)
            return text.replace(line, line.replace('received=1918989414', 'received=0'))
        with self.assertRaisesRegex(ValueError, 'forced receipt is not synchronous'):
            v.audit(self.change(2, edit_preload=edit))

    def test_settled_ownership_required(self):
        def edit(text):
            line = next(line for line in text.splitlines() if 'scene=2 label=settled ' in line)
            return text.replace(line, line.replace('received=0', 'received=1918989414'))
        with self.assertRaisesRegex(ValueError, 'settled permission ownership differs'):
            v.audit(self.change(2, edit_preload=edit))

    def test_every_public_marker_compared(self):
        with self.assertRaisesRegex(ValueError, 'permission repeat/control markers differ'):
            v.audit(self.change(2, edit_preload=lambda s: s.replace('x=672.482', 'x=672.481', 1)))

    def test_probe_source_pinned(self):
        def edit(rows):
            rows[0]['source_sha256']['itemdrop_probe.j'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'permission probe source changed'):
            v.audit(self.change(0, edit_rows=edit))

    def test_only_research_environment_allowed(self):
        with self.assertRaisesRegex(ValueError, 'permission research environment'):
            v.audit(self.change(2, edit_rows=lambda rows: rows[0].update(env='A')))
