"""Reject weakened Entangle producer witnesses before porting their owner."""
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
import entangle_producer_evidence as v


class EntangleProducerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def change(self, index=0, edit_rows=None, edit_preload=None):
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

    def test_complete_native_matrix(self):
        self.assertEqual(v.audit(), dict(observations=2, controls=1, public_markers=561,
                                        engine_parity=False, remaining_owner='TARGET-01.2'))

    def test_control_required(self):
        data = copy.deepcopy(self.bundle)
        data['captures'].pop()
        with self.assertRaisesRegex(ValueError, 'control required'):
            v.audit(data)

    def test_map_bytes_required(self):
        data = copy.deepcopy(self.bundle)
        data['map']['raw_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'literal capture bytes changed'):
            v.audit(data)

    def test_exact_sources_required(self):
        with self.assertRaisesRegex(ValueError, 'Entangle source changed'):
            v.audit(self.change(edit_rows=lambda rows: rows[0]['source_sha256'].update({'itemdrop_probe.j': '0' * 64})))

    def test_research_environment_required(self):
        with self.assertRaisesRegex(ValueError, 'research environment'):
            v.audit(self.change(edit_rows=lambda rows: rows[0].update(env='A')))

    def test_complete_probe_required(self):
        with self.assertRaisesRegex(ValueError, 'incomplete Entangle public stream'):
            v.audit(self.change(edit_preload=lambda s: s.replace('label=complete', 'label=incomplete')))

    def test_unhooked_control_required(self):
        with self.assertRaisesRegex(ValueError, 'control has observer hooks'):
            v.audit(self.change(2, edit_rows=lambda rows: rows.insert(1, {'event': 'trace-end', 'installed': True, 'counts': {}})))

    def test_configured_range_required(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'target-range-query')['range'] = v.word(501)
        with self.assertRaisesRegex(ValueError, 'configured range differs'):
            v.audit(self.change(edit_rows=edit))

    def test_point_range_required(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'point-task')['range'] = v.word(500)
        with self.assertRaisesRegex(ValueError, 'captured placement/range differs'):
            v.audit(self.change(edit_rows=edit))

    def test_retained_target_action_required(self):
        def edit(rows):
            rows.pop(next(i for i, row in enumerate(rows) if row.get('event') == 'target-task'))
        with self.assertRaisesRegex(ValueError, 'retained target action differs'):
            v.audit(self.change(edit_rows=edit))

    def test_post_approach_facing_task_required(self):
        def edit(rows):
            rows.pop(next(i for i, row in enumerate(rows) if row.get('event') == 'point-task' and
                          row.get('scene') == 1 and row['code'] == 0xd018f))
        with self.assertRaisesRegex(ValueError, 'point/facing branch differs'):
            v.audit(self.change(edit_rows=edit))

    def test_every_public_marker_compared(self):
        with self.assertRaisesRegex(ValueError, 'repeats/control differ'):
            v.audit(self.change(2, edit_preload=lambda s: s.replace(' x=704.000', ' x=704.001', 1)))


class EntangleRangeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.RANGE_BUNDLE.read_bytes()))

    def change(self, index=0, edit_rows=None, edit_preload=None):
        data = copy.deepcopy(self.bundle)
        capture = data['settings']['authored']['captures'][index]
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

    def test_complete_range_matrix(self):
        self.assertEqual(v.audit_ranges(), dict(observations=4, controls=2, public_markers=1602,
                                               engine_parity=False, remaining_owner='TARGET-01.2'))

    def test_order_acceptance_is_distinct_from_placement(self):
        with self.assertRaisesRegex(ValueError, 'placement failure is not rejected admission'):
            v.audit_ranges(self.change(2, edit_preload=lambda s: s.replace('label=entangle-return accepted=1',
                                                                         'label=entangle-return accepted=0', 1)))

    def test_exact_authored_range_required(self):
        def edit(rows):
            next(row for row in rows if row['event'] == 'entangle-placement')['range'] = v.word(500)
        with self.assertRaisesRegex(ValueError, 'constructor failure differs'):
            v.audit_ranges(self.change(edit_rows=edit))

    def test_failed_placement_creates_no_task(self):
        with self.assertRaisesRegex(ValueError, 'failed placement constructed tasks'):
            v.audit_ranges(self.change(edit_rows=lambda rows: rows.insert(1, {'event': 'target-task'})))

    def test_control_required(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['zero']['captures'].pop()
        with self.assertRaisesRegex(ValueError, 'control required'):
            v.audit_ranges(data)

    def test_source_hash_required(self):
        with self.assertRaisesRegex(ValueError, 'range source changed'):
            v.audit_ranges(self.change(edit_rows=lambda rows: rows[0]['source_sha256'].update({'itemdrop_probe.j': '0' * 64})))

    def test_every_public_sample_compared(self):
        with self.assertRaisesRegex(ValueError, 'range repeats/control differ'):
            v.audit_ranges(self.change(2, edit_preload=lambda s: s.replace(' x=704.000', ' x=704.001', 1)))


if __name__ == '__main__':
    unittest.main()
