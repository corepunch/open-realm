"""Guard recovered native evidence without certifying the unfinished engine port."""
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
import itemdrop_motion_fixture as motion


class ItemDropEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def change(self, index, edit_rows=None, edit_preload=None):
        data = copy.deepcopy(self.bundle)
        capture = data['settings']['stock']['captures'][index]
        rows = [json.loads(line) for line in base64.b64decode(capture['raw_b64']).splitlines()]
        if edit_rows:
            edit_rows(rows)
        if edit_preload:
            raw = edit_preload(base64.b64decode(capture['preload_b64']).decode()).encode()
            capture.update(preload_b64=base64.b64encode(raw).decode(), preload_sha256=hashlib.sha256(raw).hexdigest())
            rows[-1]['sha256'] = capture['preload_sha256']
        raw = b''.join((json.dumps(row) + '\n').encode() for row in rows)
        capture.update(raw_b64=base64.b64encode(raw).decode(), raw_sha256=hashlib.sha256(raw).hexdigest())
        return data

    def test_complete_repeats_and_controls(self):
        self.assertEqual(v.audit(), dict(captures=4, controls=2, scenes=72, public_markers=3702,
                                        engine_parity=False, remaining_owner='TARGET-01.2'))

    def test_generated_motion_keeps_literal_clocks_and_mover_words(self):
        fixture = v.ROOT / 'games/warcraft-3/game/tests/fixtures/retail_itemdrop_motion.h'
        self.assertEqual(fixture.read_text(), motion.render())

    def test_missing_control(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['stock']['captures'].pop()
        with self.assertRaisesRegex(ValueError, 'repeats and an unhooked control'):
            v.audit(data)

    def test_removed_item_does_not_retire_head(self):
        data = self.change(2, edit_preload=lambda s: s.replace('label=remove-return accepted=1 head=852001',
                                                             'label=remove-return accepted=1 head=0'))
        with self.assertRaisesRegex(ValueError, 'removal retired'):
            v.audit(data)

    def test_unowned_item_still_finishes_point_journey(self):
        def edit(text):
            return text.replace('scene=10 label=settled accepted=1 head=0 x=1011.623',
                                'scene=10 label=settled accepted=1 head=0 x=809.239')
        with self.assertRaisesRegex(ValueError, 'aborted the retained point journey'):
            v.audit(self.change(2, edit_preload=edit))

    def test_undroppable_item_cannot_be_admitted(self):
        data = self.change(2, edit_preload=lambda s: s.replace('scene=8 label=drop-return accepted=0 head=0',
                                                             'scene=8 label=drop-return accepted=1 head=852001'))
        with self.assertRaisesRegex(ValueError, 'admission/head'):
            v.audit(data)

    def test_enclosed_point_does_not_release_item(self):
        raw = base64.b64decode(self.bundle['settings']['stock']['captures'][2]['preload_b64']).decode()
        line = next(line for line in raw.splitlines() if 'scene=5 label=settled' in line)
        with self.assertRaisesRegex(ValueError, 'settled item ownership'):
            v.audit(self.change(2, edit_preload=lambda s: s.replace(line, line.replace('owned=1', 'owned=0'))))

    def test_original_range_required_on_physical_task(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'point-task')['range'] = 0
        with self.assertRaisesRegex(ValueError, 'point-task/range'):
            v.audit(self.change(0, edit_rows=edit))

    def test_public_return_must_agree_with_probe(self):
        def edit(rows):
            next(row for row in rows if row.get('event') == 'public-drop-point-return')['result'] = 0
        with self.assertRaisesRegex(ValueError, 'native return contract'):
            v.audit(self.change(0, edit_rows=edit))

    def test_control_has_no_observer(self):
        with self.assertRaisesRegex(ValueError, 'observer hooks'):
            v.audit(self.change(2, edit_rows=lambda rows: rows.insert(1, dict(event='trace-end', installed=True))))

    def test_original_sources_are_pinned(self):
        def edit(rows):
            rows[0]['source_sha256']['itemdrop_probe.j'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'changed native source'):
            v.audit(self.change(1, edit_rows=edit))

    def test_only_research_environment_is_allowed(self):
        with self.assertRaisesRegex(ValueError, 'research environment'):
            v.audit(self.change(2, edit_rows=lambda rows: rows[0].update(env='A')))

    def test_literal_capture_bytes(self):
        data = copy.deepcopy(self.bundle)
        data['settings']['stock']['captures'][0]['raw_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'literal capture bytes'):
            v.audit(data)

    def test_complete_probe_required(self):
        with self.assertRaisesRegex(ValueError, 'never finished'):
            v.audit(self.change(2, edit_preload=lambda s: s.replace('label=complete', 'label=incomplete')))

    def test_every_public_sample_is_compared(self):
        with self.assertRaisesRegex(ValueError, 'repeat/control public markers'):
            v.audit(self.change(2, edit_preload=lambda s: s.replace('facing=0.429', 'facing=0.428', 1)))
