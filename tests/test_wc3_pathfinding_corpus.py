"""Corpus failures stay visible; stale reports and damaged captures never certify evidence."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from run_wc3_pathfinding_corpus import (DEFAULT_MANIFEST,load_manifest,check_report,
                                        check_capture,run_entry)


class CorpusTests(unittest.TestCase):
    def setUp(self):
        self.manifest=load_manifest(DEFAULT_MANIFEST)
        self.entry=next(e for e in self.manifest['entries'] if e['id']=='oracle-grid')
        self.target=self.manifest['target']

    def test_inventory_covers_oracles_archives_and_native_differences(self):
        entries=self.manifest['entries']
        self.assertEqual(sum(e['kind']=='oracle' for e in entries),77)
        self.assertEqual(sum(e['id'].startswith('capture-') for e in entries),92)
        self.assertEqual(sum(e['id'].startswith('live-') for e in entries),23)
        rejected=[e for e in entries if e['expected_status']=='archive-rejected']
        self.assertEqual(len(rejected),8)
        self.assertTrue(all(not e['evidence'] for e in rejected))
        completed_rejections=[e for e in rejected if e['capture_complete']]
        self.assertEqual([e['id'] for e in completed_rejections],['capture-arrival-point-first'])
        self.assertTrue(completed_rejections[0]['capture_failures'])
        native=[e for e in entries if e['expected_status']=='known-reference-difference']
        self.assertEqual(len(native),2)
        for entry in native:
            self.assertEqual(entry['expected_exit'],1)
            check_report(dict(binary_sha256=self.target['game_sha256'],differences=[{}]*4,
                              stored_size=2,promotion_disabled=False,forced_east_boundary=False),entry,self.target)

    def test_inventory_rejects_missing_scripts_changed_fixtures_and_hidden_differences(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'manifest.json'
            mutations=[]
            missing=copy.deepcopy(self.manifest)
            missing['entries']=[e for e in missing['entries'] if e['command'][1]!='tools/ghidra/verify_wc3_pathing_arrival.py']
            mutations.append(missing)
            changed=copy.deepcopy(self.manifest);changed['fixtures'][0]['sha256']='0'*64;mutations.append(changed)
            changed=copy.deepcopy(self.manifest)
            next(e for e in changed['entries'] if e['id']=='adaptive-size2')['expected_exit']=0
            mutations.append(changed)
            changed=copy.deepcopy(self.manifest);changed['entries'][0]['report']='../old.json';mutations.append(changed)
            for changed in mutations:
                path.write_text(json.dumps(changed))
                with self.assertRaises(ValueError):load_manifest(path)

    def test_capture_hash_metadata_and_completion_are_all_required(self):
        rows=[dict(event='metadata',sha256=self.target['game_sha256'],pid=99,owned=True),
              dict(event='trace-end',installed=True)]
        entry=dict(inputs=dict(metadata=dict(sha256=self.target['game_sha256'],owned=True)),capture_complete=True)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'trace.jsonl'
            def write(observed):
                path.write_text(''.join(json.dumps(r)+'\n' for r in observed))
                entry['inputs'].update(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),bytes=path.stat().st_size)
            write(rows);check_capture(path,entry)
            path.write_text(path.read_text()+'\n')
            with self.assertRaisesRegex(ValueError,'hash/length'):check_capture(path,entry)
            write(rows[:-1])
            with self.assertRaisesRegex(ValueError,'completion'):check_capture(path,entry)
            changed=copy.deepcopy(rows);changed[0]['owned']=False;write(changed)
            with self.assertRaisesRegex(ValueError,'metadata'):check_capture(path,entry)

    def test_expected_failure_does_not_accept_missing_or_changed_report(self):
        entry=next(e for e in self.manifest['entries'] if e['id']=='adaptive-size2')
        observed=dict(binary_sha256=self.target['game_sha256'],differences=[{}]*4,
                      stored_size=2,promotion_disabled=False,forced_east_boundary=False)
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);report=output/entry['report']
            context=dict(output=output,python=sys.executable,binary='controlled.dll',timeout=1)
            with patch('run_wc3_pathfinding_corpus.subprocess.run',return_value=subprocess.CompletedProcess([],1)):
                self.assertIn('missing',run_entry(entry,context,self.target)['failure'])
            def completed(command,**kwargs):
                report.write_text(json.dumps(observed))
                return subprocess.CompletedProcess(command,1)
            with patch('run_wc3_pathfinding_corpus.subprocess.run',side_effect=completed):
                result=run_entry(entry,context,self.target)
                self.assertTrue(result['verified_expected_status'])
                self.assertEqual(result['expected_status'],'known-reference-difference')
                self.assertIn('stale',run_entry(entry,context,self.target)['failure'])
                report.unlink();observed['differences']=[]
                self.assertIn('length',run_entry(entry,context,self.target)['failure'])
                report.unlink()
            with patch('run_wc3_pathfinding_corpus.subprocess.run',return_value=subprocess.CompletedProcess([],0)):
                self.assertIn('unexpected process exit',run_entry(entry,context,self.target)['failure'])

    def test_report_cannot_hide_failed_assertions_or_unexpected_mismatches(self):
        observed=dict(binary_sha256=self.target['game_sha256'],cases=1,mismatches=[])
        check_report(observed,self.entry,self.target)
        for key,value in [('binary_sha256','0'*64),('cases',0),('mismatches',[{}]),('passed',False)]:
            changed=dict(observed,**{key:value})
            with self.assertRaises(ValueError):check_report(changed,self.entry,self.target)

    def test_cli_records_the_executed_profile_verifier_source(self):
        rows=json.loads((ROOT/'tools/ghidra/fixtures/retail-ground-profile-1.27.json').read_text())['observations']
        with tempfile.TemporaryDirectory() as directory:
            archive=Path(directory);capture=archive/'profile.jsonl'
            capture.write_text(''.join(json.dumps(row)+'\n' for row in rows))
            manifest=copy.deepcopy(self.manifest)
            entry=next(e for e in manifest['entries'] if e['id']=='live-stock-profile')
            entry['inputs']=dict(capture=capture.name,sha256=hashlib.sha256(capture.read_bytes()).hexdigest(),
                                 bytes=capture.stat().st_size,metadata={k:v for k,v in rows[0].items() if k not in ('event','pid')})
            entry['command'][2]='{archive}/'+capture.name
            path=archive/'manifest.json';path.write_text(json.dumps(manifest))
            output=archive/'new'
            result=subprocess.run([sys.executable,str(ROOT/'tools/ghidra/run_wc3_pathfinding_corpus.py'),
                '--manifest',str(path),'--archive',str(archive),'--output',str(output),
                '--only','live-stock-profile'],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            summary=json.loads((output/'corpus-results.json').read_text())
            self.assertTrue(summary['passed'])
            self.assertIn('tools/frida/verify_wc3_profile_trace.py',summary['source_sha256'])
            self.assertIn('tools/ghidra/generate_wc3_math_tables.py',summary['source_sha256'])


if __name__=='__main__':unittest.main()
