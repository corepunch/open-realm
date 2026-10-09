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

    def test_complete_engine_stages_reproduce_native_header_and_frozen_differences(self):
        spec=importlib.util.spec_from_file_location('export_exclusions',ROOT/'tools/ghidra/research/export_exclusion_stages.py')
        exporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(exporter)
        raw=gzip.decompress((MODULE.FIXTURES/'retail-exclusion-stages-1.27.json.gz').read_bytes())
        self.assertEqual(hashlib.sha256(raw).hexdigest(),'3dd2f2d63bc77280d6c79bed0d2ce842a28ca3904ffd22eb98df0bd2aa4d75d6')
        stages=json.loads(raw)
        frozen=json.loads((MODULE.FIXTURES/'research/MAP-04.1-expected.json').read_text())
        self.assertEqual(exporter.header(stages,frozen),
            (ROOT/'games/warcraft-3/game/tests/retail_exclusion_stages.h').read_text())
        stages['cases'][0]['fine_cells']['after_self_inc'][0][0]^=1
        with self.assertRaises(ValueError):exporter.header(stages,frozen)

    def test_complete_original_consumers_rebuild_header_and_reject_invalid_domains(self):
        spec=importlib.util.spec_from_file_location('export_consumers',ROOT/'tools/ghidra/research/export_exclusion_consumers.py')
        exporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(exporter)
        raw=(MODULE.FIXTURES/'retail-exclusion-consumers-1.27.json').read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),MODULE.CONSUMER_SHA)
        fixture=json.loads(raw)
        self.assertEqual(exporter.header(fixture),
            (ROOT/'games/warcraft-3/game/tests/retail_exclusion_consumers.h').read_text())
        for mutate in (lambda f:f['cases'].pop(),
                       lambda f:f['cases'].__setitem__(0,f['cases'][1]),
                       lambda f:f['cases'][0].__setitem__('cls',4),
                       lambda f:f['cases'][0]['held'][0].__setitem__('endpoint_mode',0)):
            changed=json.loads(raw);mutate(changed)
            with self.assertRaises(ValueError):exporter.header(changed)

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
