import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('exclusions',ROOT/'tools/ghidra/verify_wc3_pathing_exclusions.py')
MODULE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(MODULE)


class ExclusionEvidenceTests(unittest.TestCase):
    def test_engine_stage_cells_are_exact_complete_native_snapshots(self):
        frozen=json.loads((MODULE.FIXTURES/'research/MAP-04.1-expected.json').read_text())
        expected=[]
        for target in (None,'A'):
            row=next(c for c in frozen['coarse_cases'] if c['self']=='A' and c['target']==target and
                     c['occupancy']=='dynamic' and c['link_order']=='ABC')
            for stage in ('after_clear_self','after_clear_target','search_entry','after_rebuild_self','after_rebuild_target'):
                cells=[0]*1360;offsets=(0,1024,1280,1344)
                for level,x,y,value in row['baseline_nonzero']:cells[offsets[level]+y*(32>>level)+x]=value
                for level,x,y,old,value in row['steps'][stage]:cells[offsets[level]+y*(32>>level)+x]=value
                expected+=cells
        source=(ROOT/'games/warcraft-3/game/tests/retail_coarse_scopes.h').read_text().split('={',1)[1]
        self.assertEqual([int(v) for v in re.findall(r'\b\d+\b',source)],expected)

    def test_frozen_payloads_and_all_retained_inputs_are_unchanged(self):
        for name,digest in MODULE.FROZEN.items():
            self.assertEqual(hashlib.sha256((MODULE.FIXTURES/'research'/name).read_bytes()).hexdigest(),digest)
        with tempfile.TemporaryDirectory() as directory:
            bundle=MODULE.restore_inputs(Path(directory));self.assertEqual(len(bundle['files']),7)

    def corrupted_bundle(self,mutate):
        bundle=json.loads(gzip.decompress((MODULE.FIXTURES/'retail-exclusion-inputs-1.27.json.gz').read_bytes()))
        mutate(bundle)
        with tempfile.TemporaryDirectory() as directory:
            fixtures=Path(directory);(fixtures/'retail-exclusion-inputs-1.27.json.gz').write_bytes(
                gzip.compress(json.dumps(bundle).encode(),mtime=0))
            with patch.object(MODULE,'FIXTURES',fixtures):
                with self.assertRaises(ValueError):MODULE.restore_inputs(fixtures/'output')

    def test_changed_raw_capture_is_rejected(self):
        self.corrupted_bundle(lambda b:b['files'].__setitem__('functions.json',b['files']['functions.json']+' '))

    def test_missing_hash_is_rejected(self):
        self.corrupted_bundle(lambda b:b['sha256'].pop('functions.json'))

    def test_escaping_filename_is_rejected(self):
        def mutate(bundle):
            bundle['files']['../outside']='bad';bundle['sha256']['../outside']=hashlib.sha256(b'bad').hexdigest()
        self.corrupted_bundle(mutate)


if __name__=='__main__':unittest.main()
