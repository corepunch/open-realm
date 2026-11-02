"""Semantic integrity checks for the frozen ORDER-01.12 Harvest ownership captures."""
from pathlib import Path
import base64, copy, gzip, hashlib, importlib.util, json, struct, tempfile, unittest, sys
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
SPEC = ROOT / 'tools/ghidra/fixtures/retail-harvest012-1.27.json'
sys.path.insert(0, str(ROOT / 'tools/ghidra'))
import verify_wc3_pathing_harvest012 as oracle
loader = importlib.util.spec_from_file_location('harvest012_replay', ROOT / 'tools/ghidra/research/harvest012_replay.py')
v = importlib.util.module_from_spec(loader)
loader.loader.exec_module(v)


def update(bundle, name, raw):
    bundle['files'][name] = {'sha256': hashlib.sha256(raw).hexdigest(), 'base64': base64.b64encode(raw).decode()}


def rows_change(bundle, name, change):
    rows = [json.loads(x) for x in base64.b64decode(bundle['files'][name]['base64']).decode().splitlines()]
    change(rows)
    update(bundle, name, ''.join(json.dumps(x) + '\n' for x in rows).encode())


def preload_change(bundle, name, old, new, footer):
    """Mutate one public marker and rehash both the preload file and its capture footer."""
    raw = base64.b64decode(bundle['files'][name + '-preload.txt']['base64'])
    assert raw.count(old.encode()) >= 1
    raw = raw.replace(old.encode(), new.encode(), 1)
    update(bundle, name + '-preload.txt', raw)
    if footer:
        rows_change(bundle, name + '.jsonl', lambda r: r[-1].update(sha256=hashlib.sha256(raw).hexdigest()))


def first(rows, event):
    return next(x for x in rows if x.get('event') == event)


def same_everywhere(bundle, runs, old, new):
    for run in runs:
        preload_change(bundle, run, old, new, True)


H = v.RUNS['H012']
cases = [
    ('Smart head becomes Harvest in every run', lambda b: same_everywhere(b, H, 'label=smart-mine-1 order=851971', 'label=smart-mine-1 order=852018')),
    ('one repeat differs', lambda b: preload_change(b, 'observe2', 'label=exit order=852017', 'label=exit order=852018', True)),
    ('unhashed preload edit', lambda b: preload_change(b, 'observe1', 'label=deposit order=852018', 'label=deposit order=0', False)),
    ('changed append caller', lambda b: rows_change(b, 'observe1.jsonl', lambda r: first(r, 'append-enter').update(caller='0x438a28'))),
    ('missing admission row', lambda b: rows_change(b, 'b-observe1.jsonl', lambda r: r.remove(first(r, 'admit-enter')))),
    ('instrumented control', lambda b: rows_change(b, 'control1.jsonl', lambda r: r.insert(1, {'event': 'admit-enter'}))),
    ('control lost ownership', lambda b: rows_change(b, 'b-control1.jsonl', lambda r: r[0].update(owned=False))),
    ('owner runtime lease', lambda b: rows_change(b, 'observe1.jsonl', lambda r: r[0].update(remote='127.0.0.1:27048'))),
    ('incomplete footer', lambda b: rows_change(b, 'b-observe2.jsonl', lambda r: r[-1].update(complete=False))),
    ('missing observer trace end', lambda b: rows_change(b, 'observe2.jsonl', lambda r: r.remove(first(r, 'trace-end')))),
]


class Harvest012Integrity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads(SPEC.read_text())
        cls.original = json.loads(gzip.decompress((ROOT / cls.spec['captures']).read_bytes()))

    def test_complete_repeats_and_controls_match_frozen_model(self):
        result = v.verify(ROOT / self.spec['captures'], self.spec)
        self.assertEqual((result['observations'], result['controls'], result['public_markers']), (4, 2, 26574))
        self.assertEqual(result['maps']['H012']['transitions'], 232)
        self.assertEqual(result['maps']['I012']['transitions'], 118)
        with patch.object(oracle, 'replay', return_value=v), patch.object(v, 'binary_guards', return_value=1547):
            self.assertEqual(oracle.validate(self.spec, Path('byte-guard-fixture.dll'))['maps'], result['maps'])

    def test_model_retains_witnessed_heads(self):
        h = self.spec['maps']['H012']['ownership']
        self.assertIn([47, 'exit', 852017, False], h['0'])
        self.assertIn([87, 'deposit', 852018, False], h['0'])
        self.assertIn([87, 'deposit', 0, False], h['8'])
        i = self.spec['maps']['I012']['ownership']
        self.assertIn([47, 'exit', 852018, False], i['1'])
        self.assertIn([339, 'deposit', 0, False], i['8'])

    def test_original_binary_guard_detects_changed_instruction(self):
        raw = bytearray(1536)
        struct.pack_into('<I', raw, 0x3c, 0x80)
        raw[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<H', raw, 0x86, 1)
        struct.pack_into('<H', raw, 0x94, 0xe0)
        struct.pack_into('<H', raw, 0x98, 0x10b)
        struct.pack_into('<I', raw, 0xb4, 0x6f000000)
        struct.pack_into('<IIII', raw, 0x180, 512, 0x1000, 512, 0x200)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'other.dll'
            p.write_bytes(raw)
            with self.assertRaises(AssertionError):
                v.binary_guards(p, {'6f001000': '90c3'})

    def test_semantic_mutations_rejected_after_rehash(self):
        with tempfile.TemporaryDirectory() as td:
            for label, change in cases:
                with self.subTest(case=label):
                    b = copy.deepcopy(self.original)
                    change(b)
                    p = Path(td) / 'mutant.json.gz'
                    p.write_bytes(gzip.compress(json.dumps(b).encode(), compresslevel=1, mtime=0))
                    with self.assertRaises((AssertionError, KeyError, IndexError, ValueError, StopIteration)):
                        v.verify(p, self.spec)

    def test_engine_acceptance_cannot_omit_journeys_or_save_rejection(self):
        for kind in ('empty-patterns', 'missing-save', 'empty-counts'):
            with self.subTest(case=kind):
                spec = copy.deepcopy(self.spec)
                if kind == 'empty-patterns': spec['patterns'] = []
                elif kind == 'missing-save': spec['patterns'] = ['wc3_harvest012.*']
                else: spec['engine_expected'] = {'tests': 0, 'assertions': 0}
                # Keep the real capture/model replay; byte guards have a separate
                # PE corruption test and need no installed retail DLL here.
                with patch.object(oracle, 'replay', return_value=v), patch.object(v, 'binary_guards', return_value=1547):
                    with self.assertRaises(ValueError): oracle.validate(spec, Path('byte-guard-fixture.dll'))

    def test_spec_mutation_rejected(self):
        spec = copy.deepcopy(self.spec)
        spec['maps']['I012']['ownership']['10'][3][2] = 852018
        with self.assertRaises(AssertionError):
            v.verify(ROOT / self.spec['captures'], spec)


if __name__ == '__main__':
    unittest.main()
