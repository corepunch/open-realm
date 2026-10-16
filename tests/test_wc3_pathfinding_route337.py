"""Reject incomplete or altered arrival evidence."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/ghidra'))
import verify_wc3_pathing_route337 as v


class ArrivalEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads((v.FIXTURES/'retail-route337-1.27.json').read_text())
        cls.bundle = json.loads(gzip.decompress((v.FIXTURES/'retail-route337-inputs-1.27.json.gz').read_bytes()))

    def test_complete_evidence(self):
        self.assertEqual(v.validate(self.spec, self.bundle)['arrivals'], 25)

    def mutate(self, action):
        bundle = copy.deepcopy(self.bundle)
        rows = [json.loads(s) for s in bundle['runs']['observe1']['trace'].splitlines()]
        action(rows)
        bundle['runs']['observe1']['trace'] = ''.join(json.dumps(r)+'\n' for r in rows)
        return bundle

    def test_incomplete_capture(self):
        with self.assertRaisesRegex(ValueError, 'incomplete capture'):
            v.validate(self.spec, self.mutate(lambda rows: rows.pop()))

    def test_retained_destination_change(self):
        def change(rows):
            row = next(r for r in rows if r['event'] == 'step-leave' and r['outputs'][2])
            row['path']['goal'][0] ^= 1
        with self.assertRaisesRegex(ValueError, 'changed retained route'):
            v.validate(self.spec, self.mutate(change))

    def test_missing_arrival_unlink(self):
        def change(rows):
            row = next(r for r in rows if r['event'] == 'unlink-enter' and r['caller'] == '0x16fc47')
            row['caller'] = '0x16fc00'
        with self.assertRaisesRegex(ValueError, 'arrival unlink missing'):
            v.validate(self.spec, self.mutate(change))

    def test_changed_original_reference(self):
        bundle = copy.deepcopy(self.bundle)
        bundle['oracle']['rows'][0]['outputs'][0] = 1
        with self.assertRaisesRegex(ValueError, 'oracle reference changed'):
            v.validate(self.spec, bundle)


if __name__ == '__main__':
    unittest.main()
