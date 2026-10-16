"""Retain complete reload evidence, teardown exceptions and observer controls."""
import copy
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
SPEC=importlib.util.spec_from_file_location('map_lifetime',ROOT/'tools/ghidra/verify_wc3_pathing_map_lifetime.py')
MODULE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(MODULE)


class MapLifetimeTests(unittest.TestCase):
    def test_complete_reload_report_and_observer_controls_reconstruct(self):
        result=MODULE.captures()
        self.assertEqual(result['captured_files'],10)
        self.assertEqual(result['restart_cycles'],8)
        self.assertEqual(result['releases'],9)
        self.assertEqual(result['control_markers'],219)
        self.assertEqual(result['changelevel_equal_prefix'],46)
        self.assertEqual(result['frozen_sha256'],
                         '974810fd91377a9839bb923e397e0e0255c92d67782cbc971a6bb9c41a2dc98f')

    def test_wall_clock_teardown_differences_are_kept(self):
        data=json.loads((MODULE.FIXTURES/'research/MAP-06.1-expected.json').read_text())
        cycles=data['flows']['restart']['cycles']
        self.assertEqual(len(cycles),9)
        self.assertEqual(cycles[-1]['commits'],0)
        different=[c for c in cycles[:-1] if c['equal_prefix_with_first']<c['commits']]
        self.assertEqual(len(different),5)
        self.assertTrue(all(c['tail'] for c in different))
        self.assertTrue(all(c['equal_prefix_with_first']>=43 for c in cycles[:-1]))

    def test_damaged_capture_and_divergent_control_do_not_certify_reload(self):
        original=json.loads(gzip.decompress((MODULE.FIXTURES/'retail-map-lifetime-inputs-1.27.json.gz').read_bytes()))
        with tempfile.TemporaryDirectory() as directory:
            fixtures=Path(directory);(fixtures/'research').mkdir()
            (fixtures/'research/MAP-06.1-expected.json').write_bytes(
                (MODULE.FIXTURES/'research/MAP-06.1-expected.json').read_bytes())
            for name,update_hash in [('restart-observe-1.jsonl',False),('restart-observe-1.jsonl',True),
                                      ('restart-control-1-rs-restart-prerestart.txt',True)]:
                changed=copy.deepcopy(original)
                changed['files'][name]=changed['files'][name].replace('RSPATIAL tick=','RSPATIAL tick=999',1)+'{}\n'
                if update_hash:changed['sha256'][name]=hashlib.sha256(changed['files'][name].encode()).hexdigest()
                (fixtures/'retail-map-lifetime-inputs-1.27.json.gz').write_bytes(gzip.compress(json.dumps(changed).encode()))
                with patch.object(MODULE,'FIXTURES',fixtures),self.assertRaises(ValueError):MODULE.captures()

    def test_original_release_cases_retire_owners_and_cancel_requests(self):
        data=json.loads((MODULE.FIXTURES/'retail-map-lifetime-release-1.27.json').read_text())
        self.assertEqual(data['binary_sha256'],MODULE.BINARY_SHA)
        self.assertEqual(len(data['cases']),9)
        self.assertEqual({c['width'] for c in data['cases']},{8,12})
        for case in data['cases']:
            self.assertGreater(case['records_before'],0)
            self.assertEqual(case['retired_objects'],3)
            for field in ('records_after','links_after','cells_after','owner_allocations_remaining'):
                self.assertEqual(case[field],0)
            self.assertTrue(case['request_cancelled'])
            self.assertEqual(case['external_registry_bytes'],16384)

    def test_saved_ghidra_functions_match_reproducible_mapper(self):
        fixture=json.loads((MODULE.FIXTURES/'retail-map-lifetime-ghidra-1.27.json').read_text())
        source=(ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        rows={address:(json.loads('"'+name+'"'),json.loads('"'+comment+'"'))
              for address,name,comment in re.findall(r'\{"(6f[0-9a-f]+)",\s*"((?:\\.|[^"\\])*)",\s*"((?:\\.|[^"\\])*)"\}',source)}
        self.assertFalse(fixture['unsaved']);self.assertEqual(len(fixture['rows']),5)
        for row in fixture['rows']:
            self.assertEqual((row['name'],row['comment']),rows[row['address']])


if __name__=='__main__':unittest.main()
