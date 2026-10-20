"""Reject lost original queue ownership and forged recovered captures."""
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
import verify_wc3_pathing_order022 as v


class RecoveredOrderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def reject_rows(self, index, edit):
        bundle = copy.deepcopy(self.bundle)
        capture = bundle['captures'][index]
        rows = [json.loads(line) for line in base64.b64decode(capture['raw_b64']).splitlines()]
        edit(rows)
        raw = b''.join((json.dumps(row) + '\n').encode() for row in rows)
        capture.update(raw_b64=base64.b64encode(raw).decode(), raw_sha256=hashlib.sha256(raw).hexdigest())
        with self.assertRaises(ValueError):
            v.audit(bundle)

    def test_complete_repeats_and_controls(self):
        self.assertEqual(v.audit(self.bundle), dict(captures=4, controls=2, scenes=42,
                         public_markers=1524, engine_parity=False, remaining_owner='ORDER-02.2'))
        self.assertEqual(len(v.guards()), 371)

    def test_original_admission_matrix(self):
        data = v.admission_contract()
        self.assertEqual(len(data['admission']), 128)
        self.assertEqual(len(data['owner']), 12)

    def test_inactive_owner_does_not_issue_stop(self):
        data = copy.deepcopy(v.admission_contract())
        data['owner'][-1]['events'] = []
        with self.assertRaises(ValueError):
            v.admission_contract(data)

    def test_interception_does_not_replace_head(self):
        data = copy.deepcopy(v.admission_contract())
        row = next(r for r in data['admission'] if not r['dead'] and r['depth'] == 0 and r['intercept'] and r['mode'])
        row['events'][-1]['name'] = 'cancel-all'
        with self.assertRaises(ValueError):
            v.admission_contract(data)

    def test_admission_matrix_missing_case(self):
        data = copy.deepcopy(v.admission_contract())
        data['admission'].pop()
        with self.assertRaises(ValueError):
            v.admission_contract(data)

    def test_missing_repeat(self):
        bundle = copy.deepcopy(self.bundle)
        bundle['captures'].pop()
        with self.assertRaises(ValueError):
            v.audit(bundle)

    def test_old_owner_lost(self):
        self.reject_rows(0, lambda rows: next(r for r in rows if r['event'] == 'immediate-event' and
                         r['order']['command'] == 851972)['state'].update(owner=1))

    def test_owner_stop_is_not_ordinary_admission(self):
        self.reject_rows(0, lambda rows: next(r for r in rows if r['event'] == 'admit' and
                         r['order']['command'] == 851972).update(mode=0))

    def test_repair_successors_survive(self):
        def edit(rows):
            at = next(i for i, r in enumerate(rows) if r['event'] == 'intercept-end' and r['result'] == 1)
            rows[at + 2]['state']['count'] = 2
        self.reject_rows(0, edit)

    def test_repair_task_replaced(self):
        def edit(rows):
            at = next(i for i, r in enumerate(rows) if r['event'] == 'intercept-end' and r['result'] == 1)
            rows[at + 2]['state']['tasks'] = [0xffffffff, 0xffffffff]
        self.reject_rows(0, edit)

    def test_windup_replaced(self):
        self.reject_rows(3, lambda rows: next(r for r in rows if r['event'] == 'net-order').update(fallback=0))

    def test_fallback_not_silent(self):
        self.reject_rows(3, lambda rows: next(r for r in rows if r['event'] == 'insert').update(dispatch=1))

    def test_shift_loses_origin_flag(self):
        def edit(rows):
            row = next(r for r in v.scenes(rows)[2] if r['event'] == 'append' and r['order']['command'] == 851986)
            row['order']['flags'] = 0
        self.reject_rows(3, edit)

    def test_wrong_observer_depth(self):
        self.reject_rows(0, lambda rows: next(r for r in rows if r['event'] == 'set-owner').update(depth=9))

    def test_missing_sequence(self):
        self.reject_rows(0, lambda rows: next(r for r in rows if r.get('seq') == 10).update(seq=11))

    def test_hooked_control(self):
        self.reject_rows(2, lambda rows: rows.insert(1, dict(event='module', seq=1)))

    def test_bad_input(self):
        self.reject_rows(0, lambda rows: next(r for r in rows if r['event'] == 'player-input').update(rc=1))

    def test_owner_environment(self):
        self.reject_rows(0, lambda rows: rows[0].update(env='A'))

    def test_source_changed(self):
        self.reject_rows(0, lambda rows: rows[0]['source_sha256'].update({'order022_probe.j': '0' * 64}))

    def test_authored_map_changed(self):
        self.reject_rows(0, lambda rows: rows[0]['source_sha256'].update({'map': '0' * 64}))

    def test_input_helper_changed(self):
        self.reject_rows(0, lambda rows: rows[0]['source_sha256'].update({'order022-ui.exe': '0' * 64}))

    def test_incomplete_preload(self):
        self.reject_rows(0, lambda rows: rows[-1].update(complete=False))

    def test_literal_preload_bytes_are_not_normalized(self):
        bundle = copy.deepcopy(self.bundle)
        capture = bundle['captures'][0]
        raw = base64.b64decode(capture['preload_b64'])
        self.assertIn(b'\r\n', raw)
        capture['preload_b64'] = base64.b64encode(raw.replace(b'\r\n', b'\n')).decode()
        with self.assertRaises(ValueError):
            v.audit(bundle)


if __name__ == '__main__':
    unittest.main()
