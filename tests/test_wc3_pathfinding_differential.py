"""Fresh actual game traces and strict retail/engine report comparison."""
from pathlib import Path
import copy
import json
import sys
import os
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import wc3_pathfinding_differential as diff
TEST='wc3_movement.public_same_cell_routes_retain_single_points_and_saved_motion'

class DifferentialTests(unittest.TestCase):
    def test_game_exports_actual_commit_journal(self):
        binary=ROOT/'build/bin/openwarcraft3-tests'
        if not binary.exists():self.skipTest('build openwarcraft3-tests first')
        with tempfile.TemporaryDirectory(prefix='wc3-diff-test-')as tmp:
            output=Path(tmp)/'motion.jsonl'
            result=subprocess.run([str(binary),'-data','build/tests','+dedicated','1','+test',TEST],cwd=ROOT,
                env=dict(os.environ,WC3_PATHFINDING_TRACE=str(output)),capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertTrue(output.exists(),'game must export actual callback observations')
            actual=diff.engine_report(output)
            expected=diff.load(diff.FIXTURES/'retail-differential-baseline-115.json')
            self.assertTrue(diff.compare(expected,actual)['passed'])
            rows=diff.read_rows(output.read_bytes())
            for mutate in ('footer','profile','extra','clock','begin_bool','profile_order'):
                bad=copy.deepcopy(rows)
                if mutate=='footer':bad.pop()
                elif mutate=='profile':next(r for r in bad if r['event']=='profile')['window']^=1
                elif mutate=='extra':bad.insert(-1,dict(event='unknown'))
                elif mutate=='begin_bool':bad[0]['clock'][1]=False
                elif mutate=='profile_order':bad[1],bad[2]=bad[2],bad[1]
                else:next(r for r in bad if r['event']=='velocity-commit')['clock'][2]=True
                output.write_text(''.join(json.dumps(r)+'\n'for r in bad))
                with self.assertRaises(ValueError):diff.engine_report(output)

    def test_saved_ghidra_boundary_and_reproducer_are_retained(self):
        evidence=diff.load(diff.FIXTURES/'retail-differential-adapter-115-static.json')
        self.assertFalse(evidence['readback']['unsaved'])
        funcs={f['address']:f for f in evidence['readback']['functions']}
        self.assertEqual(set(funcs),{'6f16fe20','6f15f7e0','6f1606e0'})
        self.assertIn('16fe20 return boundary',funcs['6f15f7e0']['comment'])
        self.assertIn('16fe20 return boundary',(ROOT/'tools/ghidra/MapPathfinding.java').read_text())
        self.assertTrue(funcs['6f15f7e0']['xrefs'])

    def baseline(self):return diff.load(diff.FIXTURES/'retail-differential-baseline-115.json')

    def test_two_complete_retail_captures_encode_identical_reports(self):
        baseline=self.baseline()
        for tag in ('b','c'):
            actual=diff.retail_report(diff.FIXTURES/f'retail-cached-fine-route-1.27-live-{tag}.jsonl.gz')
            self.assertTrue(diff.compare(baseline,actual)['passed'])
            self.assertEqual(len(actual['events']),28)

    def test_every_simulation_word_is_exact_and_has_failure_context(self):
        baseline=self.baseline()
        for i in range(28):
            for key,n in [('clock',3),('position',2),('velocity',2),('heading',1)]:
                for k in range(n):
                    bad=copy.deepcopy(baseline)
                    if n==1:bad['events'][i][key]^=1
                    else:bad['events'][i][key][k]^=1
                    result=diff.compare(baseline,bad)
                    self.assertFalse(result['passed'])
                    self.assertEqual(result['difference_count'],1)
                    d=result['differences'][0]
                    self.assertTrue(d['path'].startswith(f'/events/{i}/{key}'))
                    self.assertEqual(d['event_context']['sequence'],i)
                    self.assertIn('expected_hex',d)

    def test_missing_extra_reordered_events_identity_type_and_signed_zero_fail(self):
        base=self.baseline()
        for case in ('missing','extra','reorder','life','sequence','kind','field','bool','float','signedzero','clockepoch','input'):
            bad=copy.deepcopy(base)
            if case=='missing':bad['events'].pop()
            elif case=='extra':bad['events'].append(copy.deepcopy(bad['events'][-1]))
            elif case=='reorder':bad['events'][0],bad['events'][1]=bad['events'][1],bad['events'][0]
            elif case=='life':bad['events'][7]['actor']=[0,0]
            elif case=='sequence':bad['events'][1]['sequence']=0
            elif case=='kind':bad['events'][0]['event']='position-query'
            elif case=='field':bad['events'][0]['RNG']=42
            elif case=='bool':bad['events'][0]['clock'][1]=False
            elif case=='float':bad['events'][0]['clock'][1]=0.0
            elif case=='signedzero':bad['events'][6]['velocity'][0]=0x80000000
            elif case=='clockepoch':bad['events'][0]['clock'][1]=1
            else:bad['inputs']['producer']['primary_step_ms']=10
            with self.subTest(case=case):self.assertFalse(diff.compare(base,bad)['passed'])

    def test_presentation_exemption_is_bounded_and_diagnostics_are_not_acceptance(self):
        base=self.baseline();bad=copy.deepcopy(base)
        bad['presentation']={'wall_ms':999,'swap_intervals_ms':[16,32],'image_sha256':'0'*64}
        bad['provenance']['pid']=123
        self.assertTrue(diff.compare(base,bad)['passed'])
        bad['presentation']['pose']=[0,0]
        self.assertFalse(diff.compare(base,bad)['passed'])
        bad=copy.deepcopy(base)
        for e in bad['events']:
            for k in range(3):e['clock'][k]^=1
        result=diff.compare(base,bad)
        self.assertFalse(result['passed']);self.assertEqual(result['difference_count'],84)
        self.assertEqual(len(result['differences']),32)

    def test_run_requires_fresh_output_and_rejects_zero_exit_failed_tests(self):
        with tempfile.TemporaryDirectory(prefix='wc3-diff-fail-')as tmp:
            tmp=Path(tmp);binary=tmp/'fake-engine'
            binary.write_text('#!/bin/sh\necho "=== 0/1 assertions passed in 1 test(s) ==="\n')
            binary.chmod(0o755)
            output=tmp/'run'
            with self.assertRaisesRegex(ValueError,'did not pass'):diff.run(binary,ROOT/'build/tests',output)
            self.assertTrue((output/'engine.log').exists())
            self.assertFalse((output/'comparison.json').exists())
            with self.assertRaises(FileExistsError):diff.run(binary,ROOT/'build/tests',output)

    def test_cli_mismatch_returns_nonzero_with_a_fresh_diagnostic(self):
        with tempfile.TemporaryDirectory(prefix='wc3-diff-cli-')as tmp:
            tmp=Path(tmp);bad=self.baseline();bad['events'][0]['position'][0]^=1
            diff.write(tmp/'bad.json',bad)
            result=subprocess.run([sys.executable,str(ROOT/'tools/wc3_pathfinding_differential.py'),
                'compare',str(diff.FIXTURES/'retail-differential-baseline-115.json'),str(tmp/'bad.json'),str(tmp/'result.json')],capture_output=True,text=True)
            self.assertEqual(result.returncode,1)
            self.assertFalse(diff.load(tmp/'result.json')['passed'])

if __name__=='__main__':unittest.main()
