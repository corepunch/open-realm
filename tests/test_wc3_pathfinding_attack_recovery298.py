"""Portable F19 service-boundary evidence checks; no Unicorn or retail DLL needed."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'tools/ghidra/research'
sys.path.insert(0, str(RESEARCH))
spec = importlib.util.spec_from_file_location('attack_recovery298_oracle', RESEARCH / 'attack_recovery298_oracle.py')
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)
sys.path.pop(0)
DATA = json.loads(oracle.FIXTURE.read_text())


class AttackRecovery298Evidence(unittest.TestCase):
    def test_complete_fixture_and_provenance(self):
        oracle.validate(DATA)
        provenance = oracle.verify_provenance()
        self.assertEqual(provenance['coverage']['producer_cases'], 81)
        self.assertEqual(provenance['coverage']['original_task_pop_cases'], 7)
        self.assertIn('Not exercised', provenance['adjusted_range_scope']['status'])

    def test_original_direct_call_bytes(self):
        data = json.loads((oracle.FIXTURE.parent / 'retail_attack_recovery298_static.json').read_text())
        rows = data['direct_call_sites']
        self.assertEqual(data['original_sha256'], oracle.SHA256)
        self.assertEqual(len(rows), 17)
        self.assertEqual(len({r['caller_function_rva'] for r in rows}), 15)
        self.assertEqual(len({r['call_instruction_rva'] for r in rows}), 17)
        for row in rows:
            raw = bytes.fromhex(row['original_instruction_hex'])
            self.assertEqual(len(raw), 5)
            self.assertEqual(raw[0], 0xe8)
            target = int(row['call_instruction_rva'], 16) + 5 + struct.unpack('<i', raw[1:])[0]
            self.assertEqual(target, 0x49d280)
            self.assertEqual(row['decoded_target_rva'], '0x49d280')

    def test_nine_native_oracle_negative_controls(self):
        self.assertEqual(oracle.negative_controls(DATA['rows']), DATA['negative_controls'])

    def test_equal_and_adjacent_slot_inputs(self):
        self.assertEqual([r['range_input_words'] for r in DATA['rows'][-5:]], [
            [0x43a68000, 0x43a68000], [0x43a67fff, 0x43a68000],
            [0x43a68000, 0x43a67fff], [0x43a68000, 0x43a68001],
            [0x43a68001, 0x43a68000]])


def mutation_test(kind):
    def test(self):
        d = copy.deepcopy(DATA)
        if kind == 'missing': d['rows'].pop()
        elif kind == 'extra': d['rows'].append(copy.deepcopy(d['rows'][0]))
        elif kind == 'reorder': d['rows'][0], d['rows'][1] = d['rows'][1], d['rows'][0]
        elif kind == 'input_word': d['rows'][-1]['range_input_words'][0] ^= 1
        elif kind == 'maximum_word': d['rows'][-1]['maximum_range_word'] ^= 1
        elif kind == 'capture_request': d['rows'][0]['inputs']['capture'] ^= 1
        elif kind == 'initializer': d['initializer']['word_after'] ^= 1
        elif kind == 'pop_missing': d['pop_rows'].pop()
        elif kind == 'pop_extra': d['pop_rows'].append(copy.deepcopy(d['pop_rows'][0]))
        elif kind == 'pop_reorder': d['pop_rows'][0], d['pop_rows'][1] = d['pop_rows'][1], d['pop_rows'][0]
        elif kind == 'pop_result': d['pop_rows'][0]['result'] ^= 1
        elif kind == 'pop_input': d['pop_rows'][0]['before'][0] ^= 1
        elif kind == 'point_word':
            row = next(r for r in d['rows'] if any(isinstance(s, dict) and 'point_words' in s for s in r['services']))
            next(s['point_words'] for s in row['services'] if isinstance(s, dict) and 'point_words' in s)[1] ^= 1
        else: raise AssertionError(kind)
        with self.assertRaises(AssertionError):
            oracle.validate(d)
    return test


for _kind in ('missing', 'extra', 'reorder', 'input_word', 'maximum_word', 'capture_request',
              'initializer', 'pop_missing', 'pop_extra', 'pop_reorder', 'pop_result', 'pop_input', 'point_word'):
    setattr(AttackRecovery298Evidence, 'test_rejects_' + _kind, mutation_test(_kind))

if __name__ == '__main__':
    unittest.main()
