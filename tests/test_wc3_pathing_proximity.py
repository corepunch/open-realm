import gzip
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('proximity',ROOT/'tools/ghidra/verify_wc3_pathing_proximity.py')
MODULE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(MODULE)


class ProximityEvidenceTests(unittest.TestCase):
    def test_every_engine_visit_and_authored_input_is_frozen(self):
        expected=json.loads(gzip.decompress((MODULE.FIXTURES/'research/SEP-02.2-expected.json.gz').read_bytes()))
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);MODULE.restore_inputs(root)
            self.assertEqual(MODULE.check_fixture(expected,root),3055)

    def corrupt_bundle(self,mutate):
        bundle=json.loads(gzip.decompress((MODULE.FIXTURES/'retail-proximity-inputs-1.27.json.gz').read_bytes()))
        mutate(bundle)
        class Fixture:
            def __truediv__(self,name):return self
            def read_bytes(self):return gzip.compress(json.dumps(bundle).encode())
        with tempfile.TemporaryDirectory() as directory,patch.object(MODULE,'FIXTURES',Fixture()):
            with self.assertRaises(ValueError):MODULE.restore_inputs(Path(directory))

    def test_missing_hash_is_rejected(self):
        self.corrupt_bundle(lambda b:b['sha256'].pop(next(iter(b['sha256']))))

    def test_corrupted_capture_is_rejected(self):
        self.corrupt_bundle(lambda b:b['files'].__setitem__('triad-observe-1/capture.jsonl','{}\n'))

    def test_unsafe_capture_path_is_rejected(self):
        def change(bundle):
            name=next(iter(bundle['files']));bundle['files']['../escaped']=bundle['files'].pop(name)
            bundle['sha256']['../escaped']=bundle['sha256'].pop(name)
        self.corrupt_bundle(change)

    def test_partial_body_stream_is_rejected(self):
        import sys
        sys.path.insert(0,str(ROOT/'tools/frida/research'))
        from sep_research_analyze import Capture
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);MODULE.restore_inputs(root)
            cap=Capture(root/'triad-observe-1',root/'map.json');MODULE.validate_capture(cap)
            cap.updates=cap.updates[:-1]
            with self.assertRaises(ValueError):MODULE.validate_capture(cap)

    def test_saved_ghidra_annotations_cover_the_engine_mapping(self):
        saved=json.loads((MODULE.FIXTURES/'retail-proximity-ghidra-1.27.json').read_text())
        self.assertFalse(saved['unsaved']);self.assertEqual(len(saved['rows']),5)
        mapping=(ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        for row in saved['rows']:
            self.assertIn(row['address'],mapping)
            self.assertIn('Payoff132',row['comment'])


if __name__=='__main__':unittest.main()
