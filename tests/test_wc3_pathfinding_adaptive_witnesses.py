"""Frozen adaptive handoff coverage and exported C expectations stay intact."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
FIXTURES=ROOT/'tools/ghidra/fixtures/research'


def fixture(name):
    data=(FIXTURES/name).read_bytes()
    if name.endswith('.gz'):data=gzip.decompress(data)
    return json.loads(data)


class AdaptiveWitnessTests(unittest.TestCase):
    def test_every_reachable_outcome_has_a_producer_witness(self):
        inventory=fixture('ACC-01.1-expected.json')
        witness=fixture('ACC-01.2-expected.json.gz')
        reach=collections.Counter(r['reach'] for r in inventory['outcomes'])
        self.assertEqual(sum(reach.values()),578)
        self.assertEqual(sum(v for k,v in reach.items() if k.startswith('R')),463)
        self.assertEqual(sum(v for k,v in reach.items() if k.startswith('U')),113)
        self.assertEqual(reach['C-out'],2)
        self.assertEqual(len(witness['rows']),3288)
        self.assertEqual(set(r['lane'] for r in witness['rows']),{0,2,4,6})
        for row in inventory['outcomes']:
            key=row['va']+':'+row['outcome']
            self.assertTrue(row['meaning'])
            self.assertEqual(key in witness['jcc_witnesses'],row['reach'].startswith('R'),key)
        self.assertEqual(len(witness['transitions']),276)
        self.assertEqual(set(witness['transitions'].values()),{'observed'})
        self.assertEqual(len(witness['details']),10)
        self.assertTrue(all(row['nodes'] for row in witness['details']))

    def test_portable_ghidra_inputs_preserve_original_provenance(self):
        inventory=fixture('ACC-01.1-expected.json');bundle=fixture('ACC-01.1-inputs.json.gz')['files']
        self.assertEqual(len(bundle),59)
        for name,digest in inventory['disassembly_sha256'].items():
            self.assertEqual(hashlib.sha256(bundle['ghidra/'+name+'-disassemble_function.txt'].encode()).hexdigest(),digest)
        for name,source in inventory['corpora'].items():
            data=bundle['coverage/'+name+'.json']
            self.assertEqual(hashlib.sha256(data.encode()).hexdigest(),source['coverage_sha256'])
            self.assertEqual(json.loads(data)['exit_status'],source['exit_status'])

    def test_marker_and_cost_scope_is_complete_without_grid_optimum_claim(self):
        markers=fixture('ACC-02.2-expected.json');costs=fixture('ACC-04.1-expected.json.gz')
        self.assertEqual(markers['A_child_states']['reachable_child_states'],108)
        self.assertEqual(len(markers['B_reducer_paths']['rows']),96)
        self.assertEqual(markers['C_levels']['nonzero_marker_bytes_levels_1_3'],[0,0,0])
        self.assertEqual(costs['part1_isqrt']['samples'],135981)
        self.assertEqual(costs['part1_isqrt']['differs_from_floor_sqrt'],0)
        self.assertEqual(costs['part2_costs']['retail_equals_explored_optimum'],188)
        self.assertEqual(costs['part2_costs']['retail_above_explored_optimum'],[])
        self.assertEqual(len(costs['part4_budget']['rows']),157)
        self.assertTrue(all(r['nearest_matches_min_d2_first_created'] for r in costs['part4_budget']['rows']))
        self.assertEqual([r['costs'] for r in costs['part3_ties']['asymmetric_cost_pairs']],
            [[880,891],[845,849],[894,895],[905,920]])

    def test_c_export_matches_all_frozen_routes_and_rare_nodes(self):
        subprocess.run([sys.executable,str(ROOT/'tools/ghidra/generate_wc3_adaptive_witnesses.py'),'--check'],check=True)

    def test_saved_ghidra_prototypes_match_portable_schema(self):
        readback=json.loads((ROOT/'tools/ghidra/fixtures/retail-adaptive-witnesses-ghidra-1.27.json').read_text())
        schema=json.loads((ROOT/'tools/ghidra/fixtures/retail-pathfinding-types-1.27.json').read_text())
        mapping=(ROOT/'tools/ghidra/MapPathfinding.java').read_text()
        self.assertFalse(readback['unsaved_changes'])
        self.assertEqual(len(readback['functions']),31)
        typed=0
        for function in readback['functions']:
            self.assertIn(function['name'],mapping)
            method=next((m for m in schema['methods'] if m['address']==function['address']),None)
            if method is None:continue
            typed+=1
            self.assertEqual(function['convention'],method['convention'])
            self.assertEqual([p['name'] for p in function['parameters']],
                [p['name'] for p in method['parameters']])
            self.assertEqual(function['parameters'][0]['storage'],'ECX:4')
            self.assertNotEqual(function['returns'],'undefined')
        self.assertEqual(typed,26)


if __name__=='__main__':unittest.main()
