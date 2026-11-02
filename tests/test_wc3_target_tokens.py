"""Native target-table evidence must reject changed masks and incomplete guards."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
module_spec = importlib.util.spec_from_file_location('wc3_target_tokens', ROOT / 'tools/ghidra/verify_wc3_pathing_target_tokens.py')
module = importlib.util.module_from_spec(module_spec)
module_spec.loader.exec_module(module)


class TargetTokensTests(unittest.TestCase):
    def setUp(self):
        self.spec = json.loads((ROOT / module.SPEC).read_text())

    def test_complete_original_table_matches_typed_slk_cases(self):
        self.assertTrue(module.verify(self.spec)['passed'])

    def test_changed_mask_is_rejected(self):
        changed = copy.deepcopy(self.spec)
        changed['tokens'][6]['mask'] ^= 0xc0000000
        with self.assertRaisesRegex(AssertionError, 'table word'):
            module.verify(changed)

    def test_missing_parser_instruction_is_rejected(self):
        changed = copy.deepcopy(self.spec)
        changed['parser_instructions'].pop()
        with self.assertRaisesRegex(AssertionError, 'incomplete parser'):
            module.verify(changed)

    def test_changed_instruction_is_rejected(self):
        changed = copy.deepcopy(self.spec)
        changed['parser_instructions'][0]['bytes'] = '54'
        with self.assertRaisesRegex(AssertionError, 'parser byte digest'):
            module.verify(changed)
