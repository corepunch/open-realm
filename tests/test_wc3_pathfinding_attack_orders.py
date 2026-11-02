import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class AttackOrderEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.verifier = load('attack173_verifier', 'tools/ghidra/research/verify_attack173_orders.py')
        cls.claims = load('attack173_claims', 'tools/frida/research/order0110_verify.py')
        cls.frozen = json.loads((ROOT / cls.verifier.EXPECTED).read_text())

    def test_frozen_complete_ownership_claims(self):
        self.assertEqual(hashlib.sha256((ROOT / self.verifier.EXPECTED).read_bytes()).hexdigest(), self.verifier.SHA)
        self.assertEqual(self.claims.check(self.frozen), [])
        self.assertEqual(len(self.claims.CLAIMS), 15)

    def test_head_point_and_damage_ownership_mutations_are_rejected(self):
        for field in ('order', 'point', 'damage', 'transition', 'identity'):
            data = copy.deepcopy(self.frozen)
            if field == 'order': data['scenes']['v3a']['timelines']['0']['transitions'][1]['order'] ^= 1
            elif field == 'point':
                tasks = data['scenes']['v3a']['decisions']['3']
                task = next(t for t in tasks if t.get('event') == 'user-head-dispatch-begin' and t.get('command') == 851983)
                task['point'][0] ^= 1
            elif field == 'damage': data['scenes']['v3c']['timelines']['6']['damage'][0]['source_order'] = 851983
            elif field == 'transition': data['scenes']['v3d']['timelines']['8']['transitions'].pop()
            else: data['scenes']['v3a']['words_identical'] = False
            with self.subTest(field=field): self.assertTrue(self.claims.check(data))

    def test_control_and_queue_limits_are_explicit(self):
        for name, scene in self.frozen['scenes'].items():
            self.assertEqual(len(scene['captures']), 2)
            self.assertEqual(len(scene['controls']), 0 if name == 'q1' else 1)
            self.assertEqual(scene['decisions_identical'], name != 'q1')


if __name__ == '__main__': unittest.main()
