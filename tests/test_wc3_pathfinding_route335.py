"""Reject incomplete, changed or incorrectly normalized native route evidence."""
import copy
import gzip
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_route335 as v


class RouteLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads((v.FIXTURES / 'retail-route335-1.27.json').read_text())
        cls.bundle = json.loads(gzip.decompress((v.FIXTURES / 'retail-route335-inputs-1.27.json.gz').read_bytes()))

    def test_complete_repeated_public_lifetimes(self):
        result = v.validate(self.spec, self.bundle)
        self.assertEqual(result['scenes']['335']['cache'], 2605)
        self.assertEqual(result['scenes']['336']['cache'], 1476)

    def changed_trace(self, number, run, mutate):
        spec, bundle = copy.deepcopy(self.spec), copy.deepcopy(self.bundle)
        rows = [json.loads(s) for s in bundle['scenes'][number][run]['trace'].splitlines()]
        mutate(rows)
        text = ''.join(json.dumps(r) + '\n' for r in rows)
        bundle['scenes'][number][run]['trace'] = text
        spec['scenes'][number][run]['trace'] = v.sha(text.encode())
        return spec, bundle

    def test_incomplete_run_rejected_even_with_updated_digest(self):
        spec, bundle = self.changed_trace('335', 'observe1', lambda rows: rows.pop())
        with self.assertRaisesRegex(ValueError, 'incomplete capture'):
            v.validate(spec, bundle)

    def test_changed_cache_index_rejected(self):
        def mutate(rows):
            row = next(r for r in rows if r['event'] == 'advance-leave')
            row['path']['index'] += 1
        spec, bundle = self.changed_trace('335', 'observe2', mutate)
        with self.assertRaisesRegex(ValueError, 'semantic repeat'):
            v.validate(spec, bundle)

    def test_hooked_control_rejected(self):
        spec, bundle = self.changed_trace('335', 'control', lambda rows: rows.insert(1, {'event': 'module'}))
        with self.assertRaisesRegex(ValueError, 'control attached'):
            v.validate(spec, bundle)

    def test_rebuilt_map_must_preserve_accepted_content(self):
        spec, bundle = copy.deepcopy(self.spec), copy.deepcopy(self.bundle)
        scene = bundle['scenes']['336']
        meta = json.loads(scene['rebuild_metadata'])
        meta['map_sha256'] = '0' * 64
        scene['rebuild_metadata'] = json.dumps(meta)
        spec['scenes']['336']['rebuild_metadata_sha256'] = v.sha(scene['rebuild_metadata'].encode())
        with self.assertRaisesRegex(ValueError, 'rebuilt map changed'):
            v.validate(spec, bundle)

    def test_pointer_normalization_preserves_aliases(self):
        a = [{'seq': 0, 'path': {'id': '0x1234', 'target': '0x1234'}}]
        b = [{'seq': 0, 'path': {'id': '0x5678', 'target': '0x5678'}}]
        c = [{'seq': 0, 'path': {'id': '0x5678', 'target': '0xabcd'}}]
        self.assertEqual(v.canonical(a), v.canonical(b))
        self.assertNotEqual(v.canonical(a), v.canonical(c))

    def test_callsite_rva_is_not_normalized_as_pointer(self):
        a = [{'seq': 0, 'caller': '0x166fe5'}]
        b = [{'seq': 0, 'caller': '0x166d84'}]
        self.assertNotEqual(v.canonical(a), v.canonical(b))


if __name__ == '__main__':
    unittest.main()
