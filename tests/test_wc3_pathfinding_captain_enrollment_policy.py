"""Reject identity and reachability changes even when capture hashes are updated."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
from research.verify_captain_enrollment158_live import HASH, read_capture


class CaptainEnrollmentPolicyEvidence(unittest.TestCase):
    def fixture(self, directory):
        frozen = json.loads((ROOT / 'tools/ghidra/fixtures/retail-captain-enrollment-policy-1.27.json').read_text())
        units = ['u0', 'u1', 'u2', 'u3']
        rows = [dict(event='metadata', task='payoff158', mode='observe', owned=True,
                     sha256=HASH, source_sha256=frozen['capture_sources'])]
        rows += [dict(event='append-ai-order', tick=0, unit=u) for u in units]
        for normalized in copy.deepcopy(frozen['normalized']):
            row = normalized
            for key in ('unit', 'source', 'target', 'captain'):
                if key in row:
                    v = row[key]
                    row[key] = units[v] if isinstance(v, int) else ('0x0' if v == 'null' else 'captain-owner')
            if 'before' in row:
                row['town'] = 'town-owner' if row['event'] == 'temporary' else None
                if row['town'] is None:
                    del row['town']
                for stage in ('before', 'after'):
                    row[stage]['unit'] = row['unit']
                    row[stage]['userHead'] = [1, 1] if stage == 'before' else ([2, 2] if row['head_changed'] else [1, 1])
                    row[stage]['taskHead'] = [3, 3] if stage == 'before' else ([4, 4] if row['task_changed'] else [3, 3])
                if not row['head_present']:
                    row['after']['userHead'] = [0xffffffff, 0xffffffff]
                if not row['task_present']:
                    row['after']['taskHead'] = [0xffffffff, 0xffffffff]
                for key in ('head_changed', 'task_changed', 'head_present', 'task_present'):
                    del row[key]
            rows.append(row)
        rows += [dict(event='marker', value=m) for m in frozen['public_markers']]
        rows += [dict(event='trace-end', installed=True)]
        path = directory / 'observe.jsonl'
        preload = path.with_name('observe-preload.txt')
        preload.write_text('\n'.join('call Preload( "%s" )' % m for m in frozen['public_markers']))
        rows += [dict(event='preload-file', complete=True, sha256=hashlib.sha256(preload.read_bytes()).hexdigest())]
        self.write(path, frozen, rows)
        return path, frozen, rows

    def write(self, path, frozen, rows):
        path.write_text('\n'.join(json.dumps(r) for r in rows) + '\n')
        frozen['captures'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()

    def test_complete_semantics_are_admitted(self):
        with tempfile.TemporaryDirectory() as d:
            path, frozen, _ = self.fixture(Path(d))
            read_capture(path, 'observe', frozen)

    def test_duplicate_replacement_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path, frozen, rows = self.fixture(Path(d))
            r = next(r for r in rows if r.get('event') == 'temporary' and r['tick'] == 3)
            r['after']['userHead'] = [9, 9]
            self.write(path, frozen, rows)
            with self.assertRaisesRegex(ValueError, 'semantics differ'):
                read_capture(path, 'observe', frozen)

    def test_reachability_inversion_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path, frozen, rows = self.fixture(Path(d))
            r = next(r for r in rows if r.get('event') == 'reachability')
            r['result'] = 1 - r['result']
            self.write(path, frozen, rows)
            with self.assertRaisesRegex(ValueError, 'semantics differ'):
                read_capture(path, 'observe', frozen)

    def test_missing_detachment_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            path, frozen, rows = self.fixture(Path(d))
            del rows[next(i for i, r in enumerate(rows) if r.get('event') == 'detach')]
            self.write(path, frozen, rows)
            with self.assertRaisesRegex(ValueError, 'missing bounded'):
                read_capture(path, 'observe', frozen)

    def test_control_cannot_contain_observer_events(self):
        with tempfile.TemporaryDirectory() as d:
            path, frozen, rows = self.fixture(Path(d))
            rows[0]['mode'] = 'control'
            self.write(path, frozen, rows)
            with self.assertRaisesRegex(ValueError, 'control contains observer'):
                read_capture(path, 'control', frozen)


if __name__ == '__main__':
    unittest.main()
