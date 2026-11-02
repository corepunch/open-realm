"""Preserve adjacent authored Entangle admission boundaries."""
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
import entangle_boundary_evidence as v


class EntangleBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def change(self, setting='below', index=0, edit_rows=None, edit_preload=None):
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

    def test_complete_adjacent_native_matrix(self):
        self.assertEqual(v.audit(), dict(observations=6, controls=3, public_markers=846,
                                        engine_parity=False, remaining_owner='TARGET-01.2'))

    def test_exact_boundary_is_direct_for_rooted_source(self):
        with self.assertRaisesRegex(ValueError, 'direct/retained admission differs'):
            v.audit(self.change('equal', 2, edit_preload=lambda s:
                s.replace('tick=160 scene=0 label=entangle-return accepted=1 head=0',
                          'tick=160 scene=0 label=entangle-return accepted=1 head=852147')))

    def test_authored_range_word_required(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'target-range-query')['range'] = v.word(318)
        with self.assertRaisesRegex(ValueError, 'authored range differs'):
            v.audit(self.change(edit_rows=edit))

    def test_physical_range_stays_zero(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'point-task')['range'] = v.word(317.75)
        with self.assertRaisesRegex(ValueError, 'physical task differs'):
            v.audit(self.change(edit_rows=edit))

    def test_below_boundary_retains_approach(self):
        def edit(rows):
            rows.pop(next(i for i, row in enumerate(rows) if row.get('event') == 'point-task' and row['code'] == 0xd016b))
        with self.assertRaisesRegex(ValueError, 'physical task differs'):
            v.audit(self.change(edit_rows=edit))

    def test_map_cannot_be_substituted(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['equal']['map'] = data['settings']['below']['map']
        with self.assertRaisesRegex(ValueError, 'range/map changed'):
            v.audit(data)

    def test_exact_source_required(self):
        with self.assertRaisesRegex(ValueError, 'boundary source changed'):
            v.audit(self.change(edit_rows=lambda rows: rows[0]['source_sha256'].update({'itemdrop_probe.j': '0' * 64})))

    def test_control_has_no_observer_hooks(self):
        with self.assertRaisesRegex(ValueError, 'control has observer hooks'):
            v.audit(self.change(index=2, edit_rows=lambda rows: rows.insert(1, {'event': 'point-task'})))

    def test_all_public_samples_are_compared(self):
        with self.assertRaisesRegex(ValueError, 'repeats/control differ'):
            v.audit(self.change(index=2, edit_preload=lambda s:
                s.replace('label=sample accepted=1 head=852147 x=704.000',
                          'label=sample accepted=1 head=852147 x=704.001', 1)))
