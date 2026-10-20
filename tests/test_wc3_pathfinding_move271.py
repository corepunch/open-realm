"""Reject damaged or self-consistent but non-retail movement profile evidence."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_move271 as v

class MovementProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(v.FIXTURE.read_text())
        cls.bundle = json.loads(gzip.decompress(v.BUNDLE.read_bytes()))

    def reject(self, edit):
        bundle = copy.deepcopy(self.bundle)
        edit(bundle)
        with self.assertRaises(ValueError):
            v.verify_live(bundle, self.spec)

    def test_complete(self):
        self.assertEqual(v.verify_live(self.bundle, self.spec)['ordered_events'], 150)

    def test_source_inventory(self):
        v.validate(self.spec)

    def test_profile_is_form_not_original_unit(self):
        def edit(bundle):
            row = next(r for r in bundle['captures'][0]['rows'] if r['event'] == 'minimum' and r['tick'] == 2)
            row['rawcode'] = 0x68503237
        self.reject(edit)

    def test_unclamped_base_is_not_form_base(self):
        def edit(bundle):
            row = next(r for r in bundle['captures'][0]['rows'] if r['event'] == 'speed-input' and r['tick'] == 2)
            row['base'] = 0x435c0000  # 220 instead of native283.
        self.reject(edit)

    def test_output_low_bit(self):
        def edit(bundle):
            next(r for r in bundle['captures'][0]['rows'] if r['event'] == 'speed-output')['word'] ^= 1
        self.reject(edit)

    def test_keeps_move_after_removal(self):
        self.assertIn('label=removed current=317.000 default=237.000 form=0 type=1750086199 order=851986',
                      self.spec['markers'][-2])
        self.reject(lambda b: b['captures'][2].update(preload=b['captures'][2]['preload'].replace('order=851986', 'order=0')))

    def test_missing_profile_query(self):
        self.reject(lambda b: b['captures'][0]['rows'].pop(6))

    def test_observer_completion(self):
        self.reject(lambda b: next(r for r in b['captures'][0]['rows'] if r['event'] == 'trace-end').update(depth=1))

    def test_unowned_capture(self):
        self.reject(lambda b: b['captures'][0]['rows'][0].update(owned=False))

    def test_control_must_be_unhooked(self):
        self.reject(lambda b: b['captures'][2]['rows'].append(dict(event='trace-end', seq=76)))

    def test_observer_source(self):
        self.reject(lambda b: b['captures'][0]['rows'][0]['source_sha256'].update({'move271_observer.js':'0'*64}))

    def test_wrong_map(self):
        self.reject(lambda b: b['captures'][0]['rows'][0]['source_sha256'].update(map='0'*64))

if __name__ == '__main__':
    unittest.main()
