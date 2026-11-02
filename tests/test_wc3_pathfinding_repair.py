"""Repair ownership evidence rejects incomplete, retagged and uncontrolled runs."""
import copy
import gzip
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/frida'))
from verify_wc3_repair_trace import base_contract, queue_contract, verify


class RepairEvidence(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = json.loads((ROOT / 'tools/ghidra/fixtures/retail-repair-orders-1.27.json').read_text())
        cls.rows = []
        for cap in cls.fixture['captures']:
            raw = gzip.decompress((ROOT / cap['archive']).read_bytes())
            cls.rows.append([json.loads(s) for s in raw.splitlines()])

    def test_complete_original_archives(self):
        for rows, cap in zip(self.rows, self.fixture['captures']):
            raw = gzip.decompress((ROOT / cap['archive']).read_bytes())
            self.assertEqual(verify(raw, self.fixture, cap), cap['result'])

    def test_base_repeats_are_exact(self):
        self.assertEqual(base_contract(self.rows[0]), base_contract(self.rows[1]))

    def test_work_smart_must_retain_concrete_head(self):
        rows = copy.deepcopy(self.rows[0])
        row = next(r for r in rows if r.get('event') == 'metadata-row' and r['parent'] == 620 and r['child'] == 2)
        row['word'] = 851971
        with self.assertRaises(ValueError):
            base_contract(rows)

    def test_reject_missing_column_wrong_kind_or_word_domain(self):
        for mutation in ('remove', 'kind', 'word'):
            rows = copy.deepcopy(self.rows[0])
            index = next(i for i, r in enumerate(rows) if r.get('event') == 'metadata-row')
            if mutation == 'remove':
                del rows[index]
            elif mutation == 'kind':
                rows[index]['kind'] = 'real'
            else:
                rows[index]['word'] = -1
            with self.assertRaises(ValueError):
                base_contract(rows)

    def test_missing_worker_suffix_is_not_completion(self):
        rows = [r for r in self.rows[0] if not (r.get('event') == 'metadata-row' and r['parent'] >= 1312)]
        with self.assertRaises(ValueError):
            base_contract(rows)

    def test_missing_footer_or_completion_marker_is_rejected(self):
        for event in ('trace-end', 'metadata-marker'):
            rows = [r for r in self.rows[0] if not (r.get('event') == event and
                    (event == 'trace-end' or r.get('value') == 'PATHMETA complete'))]
            raw = ('\n'.join(json.dumps(r) for r in rows) + '\n').encode()
            cap = copy.deepcopy(self.fixture['captures'][0])
            cap.update(sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw))
            with self.assertRaises(ValueError):
                verify(raw, self.fixture, cap)

    def test_queue_requires_shift_and_retained_move(self):
        for rows, cap in zip(self.rows, self.fixture['captures']):
            if cap['kind'] != 'queue':
                continue
            broken = copy.deepcopy(rows)
            next(r for r in broken if r.get('event') == 'player-move-click')['shift'] = False
            with self.assertRaises(ValueError):
                queue_contract(broken)
            broken = copy.deepcopy(rows)
            # First worker's pending order cannot become current before Move ends.
            record = next(r for r in broken if r.get('event') == 'metadata-row' and r['parent'] == 41 and r['child'] == 2)
            record['word'] = 852024
            with self.assertRaises(ValueError):
                queue_contract(broken)

    def test_saved_ghidra_repair_layout_and_abi(self):
        types = json.loads((ROOT / 'tools/ghidra/fixtures/retail-repair-orders-ghidra-1.27.json').read_text())
        functions = json.loads((ROOT / 'tools/ghidra/fixtures/retail-repair-functions-ghidra-1.27.json').read_text())
        self.assertFalse(functions['unsaved_changes'])
        self.assertEqual(len(functions['functions']), 16)
        self.assertTrue(types['passed'])
        self.assertEqual(types['layouts'][0]['length'], 0x1d0)
        self.assertEqual([(f['offset'], f['length']) for f in types['layouts'][0]['fields'] if f['name'] == 'work_target'], [(0x138, 8)])
        self.assertEqual(len(types['methods']), 5)
        for method in types['methods']:
            self.assertEqual(method['parameters'][0]['storage'], 'ECX:4')
            self.assertIn('__thiscall', method['prototype'])


if __name__ == '__main__':
    unittest.main()
