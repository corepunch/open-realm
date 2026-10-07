"""Reject changed or incomplete Captain speed evidence independently of raw pins."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.verify_captain_speed160_live import DLL, check_rows


class CaptainSpeedEvidence(unittest.TestCase):
    def fixture(self, name='away'):
        frozen = json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-speed160-1.27.json').read_text())['scenes'][name]
        rows = [dict(event='metadata', task='payoff160', mode='observe', owned=True,
                     sha256=DLL, source_sha256=frozen['sources'])]
        for item in copy.deepcopy(frozen['normalized']):
            if item['event'] == 'speed':
                item['event'] = 'captain-speed'
            rows.append(item)
        rows += [dict(event='marker', value=m) for m in frozen['public_markers']]
        rows += [dict(event='trace-end', installed=True),
                 dict(event='preload-file', markers=len(frozen['public_markers']), complete=True)]
        return rows, frozen

    def reduced(self, rows):
        return next(r for r in rows if r.get('event') == 'captain-speed' and r['word'] == 0x43592c85)

    def test_all_completed_policies_are_accepted(self):
        for name in ('away', 'home', 'slow', 'drop', 'nomove'):
            with self.subTest(name=name):
                rows, frozen = self.fixture(name)
                check_rows(rows, 'observe', frozen)

    def test_unreduced_early_speed_is_rejected(self):
        rows, frozen = self.fixture()
        self.reduced(rows)['word'] = 0x43870000
        with self.assertRaisesRegex(ValueError, 'decoded policy'):
            check_rows(rows, 'observe', frozen)

    def test_rounded_instead_of_truncated_word_is_rejected(self):
        rows, frozen = self.fixture('slow')
        next(r for r in rows if r.get('event') == 'captain-speed' and r['word'] == 0x4310c858)['word'] += 1
        with self.assertRaisesRegex(ValueError, 'decoded policy'):
            check_rows(rows, 'observe', frozen)

    def test_logical_count_cannot_be_replaced_by_eligible_count(self):
        rows, frozen = self.fixture('nomove')
        self.reduced(rows)['before']['counts'][1] = 5
        with self.assertRaisesRegex(ValueError, 'decoded policy'):
            check_rows(rows, 'observe', frozen)

    def test_cargo_drop_classification_is_required(self):
        rows, frozen = self.fixture()
        for ability in self.reduced(rows)['abilities']:
            ability['present'] = True
        with self.assertRaisesRegex(ValueError, 'decoded policy'):
            check_rows(rows, 'observe', frozen)

    def test_range_entry_must_restore_speed(self):
        rows, frozen = self.fixture()
        row = next(r for r in rows if r.get('event') == 'captain-speed' and r['inputs'] and
                   r['before']['counts'][3] == 6)
        row['word'] = 0x43592c85
        with self.assertRaisesRegex(ValueError, 'decoded policy'):
            check_rows(rows, 'observe', frozen)

    def test_missing_publication_is_rejected(self):
        rows, frozen = self.fixture()
        rows.remove(self.reduced(rows))
        with self.assertRaisesRegex(ValueError, 'publications differ'):
            check_rows(rows, 'observe', frozen)

    def test_partial_capture_is_rejected(self):
        rows, frozen = self.fixture()
        rows[-1]['complete'] = False
        with self.assertRaisesRegex(ValueError, 'incomplete public'):
            check_rows(rows, 'observe', frozen)

    def test_control_must_be_uninstrumented(self):
        rows, frozen = self.fixture()
        rows[0]['mode'] = 'control'
        with self.assertRaisesRegex(ValueError, 'control contains'):
            check_rows(rows, 'control', frozen)


if __name__ == '__main__':
    unittest.main()
