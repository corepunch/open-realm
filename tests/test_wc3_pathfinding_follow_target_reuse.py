"""Follow replacement identity and synchronous retirement stay independently checked."""
import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/frida'))
from verify_wc3_follow_target_reuse_trace import render_header,verify_retirement


class FollowTargetReuseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture=json.loads((ROOT/'tools/ghidra/fixtures/retail-follow-target-reuse-1.27.json').read_text())

    def check(self,targets=None,markers=None,target_markers=None):
        c=self.fixture['cases'][0]
        verify_retirement(c['targets'] if targets is None else targets,c['markers'] if markers is None else markers,
                          c['target_markers'] if target_markers is None else target_markers)

    def test_both_public_retirement_modes_and_real_reuse(self):
        for c in self.fixture['cases']:verify_retirement(c['targets'],c['markers'],c['target_markers'])
        self.assertEqual([c['mode'] for c in self.fixture['cases']],['remove','remove','kill','kill'])
        self.assertEqual(len(self.fixture['engine_motion']),948)

    def test_same_canonical_generation_is_rejected(self):
        targets=copy.deepcopy(self.fixture['cases'][0]['targets']);targets[3]['handle']=targets[0]['handle']
        with self.assertRaises(ValueError):self.check(targets=targets)

    def test_distinct_mover_address_cannot_claim_pool_reuse(self):
        targets=copy.deepcopy(self.fixture['cases'][0]['targets']);targets[3]['target']='0x1'
        with self.assertRaises(ValueError):self.check(targets=targets)

    def test_target_retirement_must_clear_head_synchronously(self):
        marks=[r.replace('order=0','order=851971') if 'label=after_target_retirement ' in r else r for r in self.fixture['cases'][0]['markers']]
        with self.assertRaises(ValueError):self.check(markers=marks)

    def test_replacement_cannot_be_implicitly_acquired(self):
        marks=[r.replace('order=0','order=851971') if 'tick=110 label=sample ' in r else r for r in self.fixture['cases'][0]['markers']]
        with self.assertRaises(ValueError):self.check(markers=marks)
        target=[r.replace('handleAfter=','handleAfter=1') for r in self.fixture['cases'][0]['target_markers']]
        with self.assertRaises(ValueError):self.check(target_markers=target)

    def test_header_retains_every_absolute_clock_and_motion_word(self):
        self.assertEqual((ROOT/'games/warcraft-3/game/tests/retail_follow_target_reuse.h').read_text(),render_header(self.fixture))


if __name__=='__main__':unittest.main()
