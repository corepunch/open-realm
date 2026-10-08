#!/usr/bin/env python3
"""Reconstruct ORDER-01.10 from complete archived retail observations and controls.

This verifies the evidence for public Attack ownership. Engine tests certify the
integrated admission/head subset; the remaining swing/Attack Once tasks stay open.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

EXPECTED = Path('tools/ghidra/fixtures/research/ORDER-01.10-expected.json')
SHA = 'e6fecc75b2cb04f2df2630eeff9e0b21e21207a6d246804c2c5ab31a000d8b2d'
BINARY_SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def digest(value):
    return hashlib.sha256(value).hexdigest()


def check_capture(raw, frozen, summarizer):
    events = [json.loads(line) for line in raw.splitlines() if line.strip()]
    assert not any(e.get('event') == 'trace-failed' or e.get('type') == 'error' for e in events)
    assert any(e.get('event') == 'marker' and e['value'].startswith('PATHMETA complete') for e in events)
    assert digest(raw) == frozen['sha256'], 'capture bytes changed'
    meta = next(e for e in events if e.get('event') == 'metadata')
    assert meta['sha256'] == BINARY_SHA
    assert meta['source_sha256']['map'] == frozen['map_sha256']
    markers = [e['value'] for e in events if e.get('event') == 'marker' and
               e['value'].startswith(('O110 ', 'O118 '))]
    rows = {}
    for event in events:
        if event.get('event') == 'row':
            rows.setdefault(event['parent'], {})[event['child']] = event['word']
    words = [[rows[row].get(k) for k in range(10)] for row in sorted(rows)]
    decisions = summarizer.decisions(events, rows)
    assert len(words) == frozen['records'] and len(markers) == frozen['markers']
    assert digest('\n'.join(markers).encode()) == frozen['public_sha256'], 'public emission stream differs'
    assert digest(json.dumps(words).encode()) == frozen['words_sha256'], 'raw scalar/identity words differ'
    assert digest(json.dumps(decisions, sort_keys=True).encode()) == frozen['decisions_sha256'], 'task ordering differs'
    return markers, decisions


def verify(expected, archive):
    summarizer = module('attack173_summary', Path('tools/frida/research/order0110_summarize.py'))
    claims = module('attack173_claims', Path('tools/frida/research/order0110_verify.py'))
    assert expected['binary_sha256'] == BINARY_SHA
    captures = controls = records = markers_count = 0
    for name, scene in expected['scenes'].items():
        public = []
        decisions = []
        words_hashes = []
        for frozen in scene['captures']:
            raw = (archive / 'ORDER-01.10/captures' / Path(frozen['path']).name).read_bytes()
            stream, tasks = check_capture(raw, frozen, summarizer)
            public.append(stream); decisions.append(tasks); words_hashes.append(frozen['words_sha256'])
            captures += 1; records += frozen['records']; markers_count += frozen['markers']
        for frozen in scene['controls']:
            raw = (archive / 'ORDER-01.10/captures' / Path(frozen['path']).name).read_bytes()
            assert digest(raw) == frozen['sha256'], 'observer-free capture changed'
            stream = re.findall(r'call Preload\( "((?:O110|O118) [^"\r\n]*)" \)', raw.decode())
            assert len(stream) == frozen['markers']
            assert digest('\n'.join(stream).encode()) == frozen['public_sha256']
            public.append(stream); controls += 1
        assert len(scene['captures']) == 2
        assert len(scene['controls']) == (0 if name == 'q1' else 1)
        assert all(stream == public[0] for stream in public) and scene['public_identical']
        assert (len(set(words_hashes)) == 1) == scene['words_identical']
        assert all(tasks == decisions[0] for tasks in decisions) == scene['decisions_identical']
        assert summarizer.timelines(public[0]) == scene['timelines']
        first = {key: value for key, value in decisions[0].items() if key != 'factory'}
        assert first == scene['decisions'] and decisions[0].get('factory', []) == scene['order_factories']
    assert not claims.check(expected), claims.check(expected)
    return dict(passed=True, status='retail-attack-order-evidence', binary_sha256=BINARY_SHA,
                captures=captures, observer_free_comparisons=controls, records=records,
                markers=markers_count, semantic_claims=len(claims.CLAIMS),
                scope='Admission snapshots and public-head engine subset; ORDER-01.10 remains open.',
                exclusions=['Queue captures have no observer-free control and different input landing ticks.',
                            'Retail swing timing, Attack Once and non-artillery Attack Ground are not certified engine parity.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.binary: assert digest(args.binary.read_bytes()) == BINARY_SHA
    assert digest(EXPECTED.read_bytes()) == SHA
    report = verify(json.loads(EXPECTED.read_text()), args.archive)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
