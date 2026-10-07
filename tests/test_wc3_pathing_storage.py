import gzip
import importlib.util
import json
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('storage_verifier',ROOT/'tools/ghidra/verify_wc3_pathing_records.py')
storage=importlib.util.module_from_spec(spec);spec.loader.exec_module(storage)
class SpatialStorageEvidence(unittest.TestCase):
    def test_complete_observed_captures_equal_controls(self):
        result=storage.verify_captures();self.assertEqual(sum(x['control_markers'] for x in result.values()),362)
    def test_truncated_capture_cannot_relabel_its_hash(self):
        data=json.loads(gzip.decompress((ROOT/'tools/ghidra/fixtures/retail-spatial-storage-inputs-1.27.json.gz').read_bytes()))
        name='spatial_ab-observe-1.jsonl';data['files'][name]=data['files'][name].rsplit('\n',2)[0]+'\n'
        data['sha256'][name]=storage.hashlib.sha256(data['files'][name].encode()).hexdigest()
        with self.assertRaises(AssertionError):storage.verify_captures(data)
    def test_saved_program_and_frozen_original_expectations(self):
        self.assertEqual(storage.verify_saved_evidence()['saved_functions'],14)
    def test_shared_header_must_remain_independent(self):
        data=json.loads((ROOT/'tools/ghidra/fixtures/retail-spatial-storage-ghidra-1.27.json').read_text())
        next(l for l in data['layouts'] if l['name']=='WC3PathMapHeader')['length']=188
        with self.assertRaises(AssertionError):storage.verify_saved_evidence(data)
    def test_unsaved_annotations_do_not_certify_evidence(self):
        data=json.loads((ROOT/'tools/ghidra/fixtures/retail-spatial-storage-ghidra-1.27.json').read_text());data['unsaved']=True
        with self.assertRaises(AssertionError):storage.verify_saved_evidence(data)
if __name__=='__main__':unittest.main()
