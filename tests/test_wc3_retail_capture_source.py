"""Historical capture sources retain their original hashes and strict ownership."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ghidra'))
from retail_capture_source import capture_source,matches_capture_source,SHARED_BUILDER

class CaptureSources(unittest.TestCase):
    def test_existing_capture_pin_resolves_unchanged_original_bytes(self):
        spec=json.loads((ROOT/'tools/ghidra/fixtures/retail-target-normalize218-1.27.json').read_text())
        expected=spec['pins'][SHARED_BUILDER]
        path=capture_source(ROOT,SHARED_BUILDER,expected)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),expected)
        self.assertIn('source-archive',path.parts)

    def test_archive_is_hash_checked_and_never_substitutes_engine_expectations(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);old=b'captured';sha=hashlib.sha256(old).hexdigest()
            p=root/'tools/frida/research/source-archive'/sha/'make_wc3_pathfinding_map.py'
            p.parent.mkdir(parents=True);p.write_bytes(old)
            self.assertEqual(capture_source(root,SHARED_BUILDER,sha),p)
            self.assertFalse(matches_capture_source(root,'games/warcraft-3/game/tests/fixture.h',sha))
            self.assertFalse(matches_capture_source(root,SHARED_BUILDER,'../'+sha))
            p.write_bytes(b'changed')
            self.assertFalse(matches_capture_source(root,SHARED_BUILDER,sha))

    def test_live_matching_sources_require_no_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);p=root/'fixture.h';p.write_bytes(b'exact')
            expected=hashlib.sha256(p.read_bytes()).hexdigest()
            self.assertEqual(capture_source(root,'fixture.h',expected),p)
            p.write_bytes(b'changed')
            self.assertFalse(matches_capture_source(root,'fixture.h',expected))

    def test_verifier_archive_allows_only_source_pin_routing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);name='tools/ghidra/verify_wc3_pathing_sample.py'
            old=b"def validate(p,h):\n    if digest(ROOT/p)!=h:raise ValueError('changed')\n    return original_bytes()\n"
            sha=hashlib.sha256(old).hexdigest()
            archive=root/'tools/ghidra/research/source-archive'/sha/'verify_wc3_pathing_sample.py'
            archive.parent.mkdir(parents=True);archive.write_bytes(old)
            live=root/name;live.parent.mkdir(parents=True,exist_ok=True)
            changed=b'from retail_capture_source import matches_capture_source\n'+old.replace(b'digest(ROOT/p)!=h',b'not matches_capture_source(ROOT,p,h)')
            live.write_bytes(changed)
            self.assertEqual(capture_source(root,name,sha),archive)
            live.write_bytes(changed.replace(b'return original_bytes()',b'return True'))
            self.assertFalse(matches_capture_source(root,name,sha))
            live.write_bytes(changed+b'original_bytes = lambda: True\n')
            self.assertFalse(matches_capture_source(root,name,sha))

    def test_current_original_instruction_checker_retains_archived_implementation(self):
        spec=json.loads((ROOT/'tools/ghidra/fixtures/retail-work231-1.27.json').read_text())
        name='tools/ghidra/verify_wc3_pathing_target_normalize218.py'
        source=capture_source(ROOT,name,spec['pins'][name])
        self.assertIn('source-archive',source.parts)
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),spec['pins'][name])
