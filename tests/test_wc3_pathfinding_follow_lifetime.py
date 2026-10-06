import copy
import gzip
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/frida/verify_wc3_follow_lifetime_trace.py'
spec = importlib.util.spec_from_file_location('follow_lifetime_trace', SCRIPT)
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


class FollowLifetimeTraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-follow-lifetime-1.27.json').read_text())
        cls.capture = cls.fixture['captures'][0]
        cls.raw = gzip.decompress((ROOT / 'tools/ghidra/fixtures' / cls.capture['archive']).read_bytes())
        cls.rows = [json.loads(line) for line in cls.raw.splitlines()]

    def altered_word(self, tick, case, label, column, word):
        rows = copy.deepcopy(self.rows)
        marker = next(r for r in rows if r.get('event') == 'metadata-marker' and
                      f'tick={tick} case={case} label={label}' in r.get('value', ''))
        parent = int(marker['value'].split()[2][4:])
        next(r for r in rows if r.get('event') == 'metadata-row' and
             r['parent'] == parent and r['child'] == column)['word'] = word
        return rows

    def test_both_complete_repeats(self):
        for cap in self.fixture['captures']:
            raw = gzip.decompress((ROOT / 'tools/ghidra/fixtures' / cap['archive']).read_bytes())
            self.assertEqual(trace.verify(raw, self.fixture, cap), cap['result'])

    def test_truncated_public_matrix(self):
        rows = [r for r in self.rows if not (r.get('event') == 'metadata-row' and r['parent'] == 1)]
        with self.assertRaisesRegex(ValueError, 'incomplete Follow matrix'):
            trace.contract(rows)

    def test_move_must_not_acquire(self):
        with self.assertRaisesRegex(ValueError, 'explicit Move unexpectedly attacked'):
            trace.contract(self.altered_word(20, 0, 'sample', 9, 123))

    def test_smart_parent_loss_does_not_complete_combat(self):
        with self.assertRaisesRegex(ValueError, 'combat-parent loss/reuse'):
            trace.contract(self.altered_word(40, 1, 'loss_after', 2, 0))

    def test_enemy_loss_completes(self):
        with self.assertRaisesRegex(ValueError, 'enemy loss did not complete'):
            trace.contract(self.altered_word(60, 1, 'enemy_death', 2, 851971))

    def test_nested_replacement_survives(self):
        marker = next(r for r in self.rows if r.get('event') == 'metadata-marker' and
                      'case=5 label=callback_after' in r.get('value', ''))
        tick = int(marker['value'].split()[3][5:])
        with self.assertRaisesRegex(ValueError, 'old owner overwrote'):
            trace.contract(self.altered_word(tick, 5, 'callback_after', 2, 0))

    def test_fresh_subject_does_not_inherit(self):
        with self.assertRaisesRegex(ValueError, 'replacement subject adopted'):
            trace.contract(self.altered_word(81, 1, 'replacement_subject', 2, 851971))

    def test_actual_removal_caller_required(self):
        rows = copy.deepcopy(self.rows)
        for row in rows:
            if row.get('event') == 'follow-loss-source' and row['callers'][0] == 0x688373:
                row['callers'][0] = 0
        with self.assertRaisesRegex(ValueError, 'missing original removal/death caller'):
            trace.contract(rows)

    def test_hash_provenance(self):
        with self.assertRaisesRegex(ValueError, 'capture hash/length'):
            trace.verify(self.raw[:-1], self.fixture, self.capture)


if __name__ == '__main__':
    unittest.main()
