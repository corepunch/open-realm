import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('purpose172',ROOT/'tools/ghidra/research/verify_purpose172_random.py')
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)


class PurposeRandomEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.frozen=json.loads((ROOT/v.EXPECTED).read_text())

    def test_engine_vectors_are_exact_original_words(self):
        self.assertEqual((ROOT/v.HEADER).read_text(),v.render(self.frozen['oracle']))
        self.assertEqual(len(self.frozen['oracle']['reseed']),8)
        self.assertEqual(len(self.frozen['oracle']['set_random_seed']),4)
        for row in self.frozen['oracle']['reseed']+self.frozen['oracle']['set_random_seed']:
            self.assertEqual(len(row['streams']),45)

    def test_changed_purpose_identity_or_dropped_draw_is_rejected(self):
        frozen=self.frozen['live']['NUM-04.5/mixed-observe-1']
        for mutate in ('identity','missing','state','order'):
            actual=copy.deepcopy(frozen);draws=actual['stream_draws']['stream:2']
            if mutate=='identity':actual['stream_draws']['stream:1']=actual['stream_draws'].pop('stream:2')
            elif mutate=='missing':draws.pop()
            elif mutate=='state':draws[0]['after'][0]^=1
            else:draws[0],draws[1]=draws[1],draws[0]
            with self.subTest(mutate=mutate),self.assertRaises(AssertionError):v.check_live(actual,frozen)

    def test_public_reseed_table_cannot_be_replaced_by_input_seed_table(self):
        actual=copy.deepcopy(self.frozen['live']['NUM-04.5/mixed-observe-1'])
        actual['reseeds']=actual['reseeds'][:1]
        with self.assertRaises(AssertionError):v.check_live(actual,self.frozen['live']['NUM-04.5/mixed-observe-1'])

    def test_repeats_keep_different_observation_tails(self):
        live=self.frozen['live']
        self.assertNotEqual(len(live['NUM-04.5/mixed-observe-1']['stream_draws']['stream:2']),
            len(live['NUM-04.5/mixed-observe-3']['stream_draws']['stream:2']))
        self.assertIn('NUM-04.5/mixed-observe-2',live)


if __name__=='__main__':unittest.main()
