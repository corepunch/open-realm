"""Reject incomplete or behaviorally different temporary enrollment evidence."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.verify_captain_enrollment157_live import COUNTS, HASH, read_capture


class CaptainEnrollmentEvidence(unittest.TestCase):
    def fixture(self, directory):
        frozen = json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-enrollment-live-1.27.json').read_text())
        rows = [dict(event='metadata', mode='observe', task='payoff157', sha256=HASH,
                     owned=True, source_sha256=frozen['capture_sources'])]
        for row in copy.deepcopy(frozen['normalized']):
            if row['event'] == 'attach':
                row['captain'] = 'captain-identity'
            if row['event'] == 'temporary':
                row['town'] = 'town-identity'
            rows.append(row)
        path = directory / 'observe.jsonl'
        preload = path.with_name('observe-preload.txt')
        preload.write_text('\n'.join('call Preload( "%s" )' % m for m in frozen['public_markers']))
        rows += [dict(event='trace-end', installed=True, counts=COUNTS),
                 dict(event='preload-file', complete=True, sha256=hashlib.sha256(preload.read_bytes()).hexdigest())]
        self.write(path, frozen, rows)
        return path, frozen, rows

    def write(self, path, frozen, rows):
        path.write_text('\n'.join(json.dumps(r) for r in rows) + '\n')
        frozen['captures'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()

    def test_complete_normalized_capture_is_admitted(self):
        with tempfile.TemporaryDirectory() as d:
            path, frozen, _ = self.fixture(Path(d))
            read_capture(path, 'observe', frozen)

    def test_missing_application_is_rejected_even_with_updated_capture_pin(self):
        with tempfile.TemporaryDirectory() as d:
            path, frozen, rows = self.fixture(Path(d))
            del rows[next(i for i, r in enumerate(rows) if r['event'] == 'temporary')]
            self.write(path, frozen, rows)
            with self.assertRaisesRegex(ValueError, 'incomplete observer events'):
                read_capture(path, 'observe', frozen)

    def test_extra_duplicate_attachment_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path, frozen, rows = self.fixture(Path(d))
            rows.insert(1, copy.deepcopy(next(r for r in rows if r['event'] == 'attach')))
            self.write(path, frozen, rows)
            with self.assertRaisesRegex(ValueError, 'incomplete observer events'):
                read_capture(path, 'observe', frozen)

    def test_wrong_actor_or_flags_are_rejected_without_count_difference(self):
        with tempfile.TemporaryDirectory() as d:
            path, frozen, rows = self.fixture(Path(d))
            next(r for r in rows if r['event'] == 'attach')['unit'] = 3
            self.write(path, frozen, rows)
            with self.assertRaisesRegex(ValueError, 'enrollment state/order differs'):
                read_capture(path, 'observe', frozen)

    def test_observed_run_cannot_be_used_as_observer_free_control(self):
        with tempfile.TemporaryDirectory() as d:
            path, frozen, rows = self.fixture(Path(d))
            rows[0]['mode'] = 'control'
            self.write(path, frozen, rows)
            with self.assertRaisesRegex(ValueError, 'control contains observer'):
                read_capture(path, 'control', frozen)


if __name__ == '__main__':
    unittest.main()
