"""Stock movement-profile observer completion and publication contract."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_profile_trace import verify


class ProfileTraceTests(unittest.TestCase):
    def setUp(self):
        self.rows=json.loads((ROOT/'tools/ghidra/fixtures/retail-ground-profile-1.27.json').read_text())['observations']

    def test_original_footman_profile_publishes_both_masks(self):
        result=verify(self.rows)
        self.assertEqual(result['rawcode'],0x68666f6f)
        self.assertEqual(result['category'],0xca)
        self.assertEqual(result['query_mask'],2)
        self.assertEqual(result['path_mask'],0x02000002)
        self.assertEqual(result['publications'],[[0x010000ca,0x02000002]]*2)

    def test_truncation_errors_and_one_word_changes_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'completion'):verify(self.rows[:-1])
        with self.assertRaisesRegex(ValueError,'observer'):verify(self.rows+[{'type':'error'}])
        changed=copy.deepcopy(self.rows);changed.pop(1)
        with self.assertRaisesRegex(ValueError,'truncated'):verify(changed)
        for field in ('category','queryMask','objectCategory','pathMask'):
            changed=copy.deepcopy(self.rows)
            row=next(r for r in changed if r.get('event')=='movement-mask-publication')
            row[field]^=1
            with self.assertRaises(ValueError):verify(changed)


if __name__=='__main__':unittest.main()
