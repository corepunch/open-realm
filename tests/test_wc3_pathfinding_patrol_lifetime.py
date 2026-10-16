import copy
import gzip
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('patrol_lifetime_trace', ROOT / 'tools/frida/verify_wc3_patrol_lifetime_trace.py')
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


class PatrolLifetimeTraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-patrol-lifetime-1.27.json').read_text())
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

    def test_complete_repeats(self):
        for cap in self.fixture['captures']:
            raw = gzip.decompress((ROOT / 'tools/ghidra/fixtures' / cap['archive']).read_bytes())
            self.assertEqual(trace.verify(raw, self.fixture, cap), cap['result'])

    def test_issued_command_is_not_active_head(self):
        with self.assertRaisesRegex(ValueError, 'two-point order'):
            trace.contract(self.altered_word(1, 0, 'patrol', 2, 851990))

    def test_original_issued_command_required(self):
        rows = copy.deepcopy(self.rows)
        next(r for r in rows if r.get('event') == 'metadata-interception' and r['command'] == 851990)['command'] = 851991
        with self.assertRaisesRegex(ValueError, 'issued Patrol command'):
            trace.contract(rows)

    def test_loss_cannot_complete_parent(self):
        with self.assertRaisesRegex(ValueError, 'enemy loss completed'):
            trace.contract(self.altered_word(45, 1, 'loss_after', 2, 0))

    def test_actual_damage_source_required(self):
        with self.assertRaisesRegex(ValueError, 'combat/source absent'):
            trace.contract(self.altered_word(20, 1, 'sample', 8, 123))

    def test_nested_move_survives(self):
        marker = next(r for r in self.rows if r.get('event') == 'metadata-marker' and
                      'case=3 label=nested_after' in r.get('value', ''))
        tick = int(marker['value'].split()[3][5:])
        with self.assertRaisesRegex(ValueError, 'overwrote nested Move'):
            trace.contract(self.altered_word(tick, 3, 'nested_after', 2, 851991))

    def test_swapped_endpoints_required(self):
        rows = copy.deepcopy(self.rows)
        next(r for r in rows if r.get('event') == 'patrol-continuation-task')['primary'][0] ^= 1
        with self.assertRaisesRegex(ValueError, 'swap authored endpoints'):
            trace.contract(rows)

    def test_append_must_preserve_head(self):
        rows = copy.deepcopy(self.rows)
        next(r for r in rows if r.get('event') == 'patrol-append')['after']['publicHead'] = [0, 0]
        with self.assertRaisesRegex(ValueError, 'append ordering'):
            trace.contract(rows)

    def test_complete_matrix_required(self):
        rows = [r for r in self.rows if not (r.get('event') == 'metadata-row' and r['parent'] == 1)]
        with self.assertRaisesRegex(ValueError, 'incomplete Patrol matrix'):
            trace.contract(rows)

    def test_frozen_hash_required(self):
        with self.assertRaisesRegex(ValueError, 'hash/length'):
            trace.verify(self.raw[:-1], self.fixture, self.capture)


if __name__ == '__main__':
    unittest.main()
