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
SPEC=importlib.util.spec_from_file_location('profiles',ROOT/'tools/ghidra/verify_wc3_pathing_profiles.py')
MODULE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(MODULE)


class MovementProfileEvidenceTests(unittest.TestCase):
    def test_engine_bit_fixture_is_complete_original_switch(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);MODULE.restore_inputs(root)
            oracle=json.loads((root/'oracle.json').read_text())
            source=(ROOT/'games/warcraft-3/game/tests/retail_movement_profiles.h').read_text()
            source=source.split('retail_profile_bits[]={',1)[1]
            actual=[tuple(int(v,0) for v in row) for row in re.findall(r'\{(0x[0-9a-f]+),(\d+),(\d+),(\d+)\}',source)]
            self.assertEqual(actual,[(r['bits'],r['map_edx_0'],r['map_edx_1'],r['published_class']) for r in oracle['lanes']])

    def test_frozen_base_and_complete_controls_match(self):
        frozen=MODULE.FIXTURES/'research/BASE-02.1-expected.json'
        self.assertEqual(hashlib.sha256(frozen.read_bytes()).hexdigest(),MODULE.EXPECTED_SHA)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);MODULE.restore_inputs(root)
            first,norm=MODULE.profile_capture(root/'profiles-first.jsonl')
            _,repeat=MODULE.profile_capture(root/'profiles-repeat.jsonl')
            self.assertEqual(norm,repeat)
            self.assertEqual(MODULE.check_control(first,root/'profiles-control.txt',150),272)

    def test_truncated_live_capture_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);MODULE.restore_inputs(root)
            rows=[json.loads(v) for v in (root/'profiles-first.jsonl').read_text().splitlines()]
            rows=[r for r in rows if not (r.get('event')=='marker' and 'tick=75 label=sample' in r['value'])]
            path=root/'truncated.jsonl';path.write_text('\n'.join(json.dumps(r) for r in rows))
            with self.assertRaises(ValueError):MODULE.profile_capture(path)

    def corrupted_bundle(self,mutate):
        bundle=json.loads(gzip.decompress((MODULE.FIXTURES/'retail-profile-inputs-1.27.json.gz').read_bytes()))
        mutate(bundle)
        with tempfile.TemporaryDirectory() as directory:
            fixtures=Path(directory);(fixtures/'retail-profile-inputs-1.27.json.gz').write_bytes(
                gzip.compress(json.dumps(bundle).encode(),mtime=0))
            with patch.object(MODULE,'FIXTURES',fixtures):
                with self.assertRaises(ValueError):MODULE.restore_inputs(fixtures/'output')

    def test_changed_raw_capture_is_rejected(self):
        self.corrupted_bundle(lambda b:b['files'].__setitem__('profiles-first.jsonl',b['files']['profiles-first.jsonl']+' '))

    def test_missing_hash_is_rejected(self):
        self.corrupted_bundle(lambda b:b['sha256'].pop('oracle.json'))

    def test_escaping_filename_is_rejected(self):
        def mutate(bundle):
            bundle['files']['../outside']='bad';bundle['sha256']['../outside']=hashlib.sha256(b'bad').hexdigest()
        self.corrupted_bundle(mutate)


if __name__=='__main__':unittest.main()
