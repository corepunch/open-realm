"""Reject damaged or incomplete object-category evidence before certification."""
import gzip
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('categories', ROOT / 'tools/ghidra/verify_wc3_pathing_categories.py')
CATEGORIES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CATEGORIES)


class CategoriesEvidence(unittest.TestCase):
    def test_unchanged_capture_and_crlf_public_output_restore_exactly(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = CATEGORIES.restore_inputs(root)
            for name in CATEGORIES.INPUTS:
                self.assertEqual(CATEGORIES.digest((root / name).read_bytes()), bundle['sha256'][name])
            self.assertIn(b'\r\n', (root / 'control-first-preload.txt').read_bytes())
            self.assertTrue(CATEGORIES.check_capture(root / 'observe-first.jsonl'))
            self.assertTrue(CATEGORIES.check_capture(root / 'observe-repeat.jsonl'))

    def mutate_capture(self, mutation, message):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            CATEGORIES.restore_inputs(root)
            path = root / 'observe-first.jsonl'
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            mutation(rows)
            path.write_text('\n'.join(json.dumps(row) for row in rows) + '\n')
            with self.assertRaisesRegex(ValueError, message):
                CATEGORIES.check_capture(path)

    def test_truncated_capture_cannot_certify_categories(self):
        self.mutate_capture(lambda rows: rows.__setitem__(slice(None), [
            row for row in rows if row.get('event') != 'trace-end']), 'incomplete observed')

    def test_missing_action_window_cannot_certify_categories(self):
        def remove_window(rows):
            next(row for row in rows if row.get('event') == 'trace-end')['counts']['window'] = 15
        self.mutate_capture(remove_window, 'incomplete observed')

    def test_wrong_target_is_rejected(self):
        def wrong_target(rows):
            next(row for row in rows if row.get('event') == 'metadata')['sha256'] = '0' * 64
        self.mutate_capture(wrong_target, 'target/mode')

    def test_changed_public_output_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            CATEGORIES.restore_inputs(root)
            path = root / 'observe-first-preload.txt'
            path.write_bytes(path.read_bytes().replace(b'FOOT032', b'FOOT033', 1))
            with self.assertRaisesRegex(ValueError, 'public output'):
                CATEGORIES.check_capture(root / 'observe-first.jsonl')

    def test_bundle_byte_change_is_rejected_before_original_execution(self):
        raw = gzip.decompress((CATEGORIES.FIXTURES / 'retail-object-category-inputs-1.27.json.gz').read_bytes())
        bundle = json.loads(raw)
        bundle['files']['observe-first.jsonl'] += '\n'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'retail-object-category-inputs-1.27.json.gz').write_bytes(gzip.compress(json.dumps(bundle).encode()))
            with patch.object(CATEGORIES, 'FIXTURES', root), self.assertRaisesRegex(ValueError, 'capture hash differs'):
                CATEGORIES.restore_inputs(root)


if __name__ == '__main__':
    unittest.main()
