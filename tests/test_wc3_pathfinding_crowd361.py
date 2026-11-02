#!/usr/bin/env python3
"""Reject incomplete or altered natural contention parity contracts."""
import copy
import gzip
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
import verify_wc3_pathing_crowd361 as verifier


class CrowdContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec=json.loads(verifier.SPEC.read_text())
        cls.bundle=json.loads(gzip.decompress(verifier.BUNDLE.read_bytes()))

    def reject(self,change):
        bundle=copy.deepcopy(self.bundle);change(bundle)
        with self.assertRaises(ValueError):verifier.runtime(bundle,self.spec)

    def test_complete_original_repeats_fresh_observation_and_control(self):
        self.assertEqual(verifier.runtime(self.bundle,self.spec)['native_rows'],22772)

    def test_original_repeat_required(self):
        self.reject(lambda b:b['source_runs'].pop(0))

    def test_literal_retail_word_cannot_be_rebased(self):
        self.reject(lambda b:b['source_runs'][2]['rows'][10].__setitem__(4,0))

    def test_matching_but_truncated_stream_is_rejected(self):
        def change(b):
            for run in b['source_runs']:run['rows'].pop()
        self.reject(change)

    def test_floating_encoding_is_rejected(self):
        def change(b):
            for run in b['source_runs']:run['rows'][0][4]=float(run['rows'][0][4])
        self.reject(change)

    def test_public_control_must_match(self):
        self.reject(lambda b:b['captures'][3]['summary']['public'].pop())

    def test_instrumented_control_is_rejected(self):
        self.reject(lambda b:b['captures'][3]['summary'].__setitem__('observer',True))

    def test_dropped_observer_rows_are_rejected(self):
        self.reject(lambda b:b['captures'][2]['summary']['trace_end'][0]['counts'].__setitem__('gtick-dropped',1))

    def test_failed_capture_is_rejected(self):
        self.reject(lambda b:b['captures'][2]['summary'].__setitem__('failed',True))

    def test_unfinished_capture_is_rejected(self):
        self.reject(lambda b:b['captures'][2]['summary'].__setitem__('complete',False))


if __name__=='__main__':unittest.main()
