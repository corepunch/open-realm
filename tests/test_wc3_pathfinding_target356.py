"""Target pursuit evidence must preserve the observer's literal float words."""
import copy
import gzip
import json
import sys
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('target021_expected', ROOT / 'tools/frida/research/target021_expected.py')
expected = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(expected)
ANALYSIS_SPEC = importlib.util.spec_from_file_location('target021_analyze', ROOT / 'tools/frida/research/target021_analyze.py')
analyzer = importlib.util.module_from_spec(ANALYSIS_SPEC)
ANALYSIS_SPEC.loader.exec_module(analyzer)


class Target356EvidenceTests(unittest.TestCase):
    def test_literal_observer_pose_survives_readable_decimal_rounding(self):
        # Original repeated Move-on-unit counter2452. Rounded six-decimal
        # display 11.249743 reconstructs the adjacent float 4133fef3.
        scene = {'timeline': [dict(c=2452, gi=0, cd=6, unseen=0, decisions=[],
                                  self={'pos': [15.291317, 11.249743],
                                        'pos_words': [0x4174AA3C, 0x4133FEF2]})]}
        row = expected.rows_for(scene)[0]
        self.assertEqual(row[-2:], [0x4174AA3C, 0x4133FEF2])

    def test_analyzer_carries_native_pose_words_through_complete_owner_visit(self):
        path = dict(p='path', dest=[0, 0], times=[0, 0], idx=[0, 0], cnt=[1, 1])
        rows = [dict(event='metadata'),
                dict(event='marker', c=2452, value='T021 tick=430 s=2 l=10 label=begin-setup'),
                dict(event='begin-target', c=2452, self=[1, 2], target=[3, 4]),
                dict(event='gtick', c=2452, g='group', id=[5, 6], target=[3, 4], flags=0,
                     cd=6, unseen=0, path=path,
                     members=[dict(id=[1, 2], pos=[0x4174AA3C, 0x4133FEF2], path=path)]),
                dict(event='gtick-end', g='group', cd=5, unseen=0, flags=0),
                dict(event='marker', c=2453, value='T021 tick=431 s=2 l=11 label=end-cleanup')]
        scene = analyzer.analyze(rows)['scenes'][0]
        self.assertEqual(expected.rows_for(scene)[0][-2:], [0x4174AA3C, 0x4133FEF2])

    def test_legacy_display_only_reports_retain_their_declared_precision(self):
        scene = {'timeline': [dict(c=1, gi=0, cd=0, unseen=0, decisions=[],
                                  self={'pos': [15.0, 9.0]})]}
        self.assertEqual(expected.rows_for(scene)[0][-2:], [0x41700000, 0x41100000])


class Target356ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(ROOT / 'tools/ghidra'))
        verifier_spec = importlib.util.spec_from_file_location('target356_verifier', ROOT / 'tools/ghidra/verify_wc3_pathing_target356.py')
        cls.verifier = importlib.util.module_from_spec(verifier_spec)
        verifier_spec.loader.exec_module(cls.verifier)
        cls.spec = json.loads(cls.verifier.SPEC.read_text())
        cls.bundle = json.loads(gzip.decompress(cls.verifier.BUNDLE.read_bytes()))

    def rejected(self, mutate):
        bundle = copy.deepcopy(self.bundle)
        mutate(bundle)
        with self.assertRaises(ValueError):
            self.verifier.runtime(bundle, self.spec)

    def test_complete_literal_contract(self):
        self.verifier.validate(self.spec)
        self.assertEqual(self.verifier.runtime(self.bundle, self.spec)['native_rows'], 5017)

    def test_missing_original_repeat_is_rejected(self):
        self.rejected(lambda b: b['source_runs'].pop())

    def test_shortened_owner_stream_is_rejected(self):
        self.rejected(lambda b: b['source_runs'][0]['scenes']['2']['rows'].pop())

    def test_literal_word_difference_is_rejected(self):
        self.rejected(lambda b: b['source_runs'][1]['scenes']['2']['rows'][11].__setitem__(4, 0x4133FEF3))

    def test_float_encoded_word_is_rejected(self):
        def mutate(bundle):
            for run in bundle['source_runs']:
                row = run['scenes']['2']['rows'][0]
                row[3] = float(row[3])
        self.rejected(mutate)

    def test_failed_control_is_rejected(self):
        self.rejected(lambda b: b['captures'][2]['summary'].__setitem__('failed', True))

    def test_instrumented_control_is_rejected(self):
        self.rejected(lambda b: b['captures'][2]['summary'].__setitem__('observer', True))

    def test_missing_observer_finish_is_rejected(self):
        self.rejected(lambda b: b['captures'][1]['summary'].__setitem__('trace_end', []))


if __name__ == '__main__':
    unittest.main()
