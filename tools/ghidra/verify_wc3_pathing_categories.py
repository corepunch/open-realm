#!/usr/bin/env python3
"""Verify original object-category inventory and complete controlled retail captures.

This verifies retail evidence. Actual engine publication/admission/save behavior is
covered separately by wc3_object_categories; raw-link engine parity is not claimed.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / 'fixtures'
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
FROZEN = {
    'BASE-02.2': '486b75d962cb10ad8fff948c177e698290ad95907472f19f08bcca5a47c709ae',
    'FOOT-03.1': '7bd26ec0c1ba8d1327f33fd5e18947dae8d5f843072d5afdff372deb2226ffa5',
    'FOOT-03.2': 'ef379cbfc9441c0b9f27d7a2a3a63cea0c550939c12561bea08102586b190df1',
}
INPUTS = {'observe-first.jsonl', 'observe-repeat.jsonl', 'observe-first-preload.txt',
          'observe-repeat-preload.txt', 'control-first-preload.txt', 'control-first.jsonl'}


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def restore_inputs(root):
    bundle = json.loads(gzip.decompress((FIXTURES / 'retail-object-category-inputs-1.27.json.gz').read_bytes()))
    if set(bundle['files']) != INPUTS or set(bundle['sha256']) != INPUTS:
        raise ValueError('capture inventory differs')
    for name, value in bundle['files'].items():
        raw = value.encode()
        if digest(raw) != bundle['sha256'][name]:
            raise ValueError('capture hash differs: ' + name)
        (root / name).write_bytes(raw)
    return bundle


def check_capture(path):
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    metadata = [r for r in rows if r.get('event') == 'metadata']
    endings = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or metadata[0].get('sha256') != SHA or metadata[0].get('mode') != 'observe':
        raise ValueError('capture target/mode differs')
    if len(endings) != 1 or not endings[0].get('installed') or endings[0]['counts']['window'] != 16:
        raise ValueError('incomplete observed capture')
    if any(r.get('event') == 'trace-failed' or r.get('type') == 'error' for r in rows):
        raise ValueError('capture contains an observer failure')
    preload = [r for r in rows if r.get('event') == 'preload-file']
    raw = path.with_name(path.stem + '-preload.txt').read_bytes()
    if len(preload) != 1 or not preload[0].get('complete') or preload[0]['markers'] != 60 or preload[0]['sha256'] != digest(raw):
        raise ValueError('incomplete or changed public output')
    return rows


def run(script, *arguments):
    subprocess.run([sys.executable, str(script), *map(str, arguments)], check=True, stdout=subprocess.DEVNULL)


def verify(binary):
    if digest(binary.read_bytes()) != SHA:
        raise ValueError('retail binary differs')
    frozen = {}
    for task, sha in FROZEN.items():
        raw = (FIXTURES / 'research' / (task + '-expected.json')).read_bytes()
        if digest(raw) != sha:
            raise ValueError('frozen research payload differs: ' + task)
        frozen[task] = json.loads(raw)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        bundle = restore_inputs(root)
        run(HERE / 'research/verify_FOOT-03.1_eligibility.py', '--binary', binary, '--report', root / 'eligibility.json')
        eligibility = json.loads((root / 'eligibility.json').read_text())
        for key in ('composed', 'link_cap'):
            if eligibility[key] != frozen['FOOT-03.1'][key]:
                raise ValueError('original eligibility output differs: ' + key)
        if eligibility['mismatches'] or any(eligibility[key] != value for key, value in (
                ('cases', 50112), ('single_link_cases', 34944), ('chain_cases', 15120))):
            raise ValueError('original eligibility coverage differs')
        run(HERE / 'research/verify_FOOT-03.2_two_categories.py', '--binary', binary,
            '--report', root / 'regions.json', '--expected', root / 'regions-expected.json')
        if digest((root / 'regions-expected.json').read_bytes()) != FROZEN['FOOT-03.2']:
            raise ValueError('complete original composed-producer freeze differs')
        sys.path.insert(0, str(HERE.parent / 'frida/research'))
        import foot032_analyze
        control = foot032_analyze.preload_markers(root / 'control-first-preload.txt')
        if len(control) != 60:
            raise ValueError('incomplete observer-free control')
        live = []
        for name in ('observe-first', 'observe-repeat'):
            path = root / (name + '.jsonl')
            check_capture(path)
            summary = foot032_analyze.summarize(path)
            if summary['consumer_checks'] != 164 or summary['mismatches'] or summary['markers'] != control:
                raise ValueError('live consumer/public control differs: ' + name)
            if foot032_analyze.preload_markers(root / (name + '-preload.txt')) != control:
                raise ValueError('observed public output differs: ' + name)
            live.append(summary)
        if live[0]['markers'] != live[1]['markers']:
            raise ValueError('repeat differs')
        ghidra = json.loads((FIXTURES / 'retail-object-categories-ghidra-1.27.json').read_text())
        if ghidra['binary_sha256'] != SHA or ghidra['unsaved'] or len(ghidra['rows']) != 31 or len({r['address'] for r in ghidra['rows']}) != 31:
            raise ValueError('Ghidra evidence is not saved for this target')
        if len(ghidra['layouts']) != 3 or sum(len(s['fields']) for s in ghidra['layouts']) != 15:
            raise ValueError('saved category layouts differ')
        return dict(binary_sha256=SHA, passed=True, status='verified', differences=[],
                    cases=50112, composed_scenarios=16, live_captures=2,
                    live_consumer_checks=sum(s['consumer_checks'] for s in live), control_markers=60,
                    saved_functions=len(ghidra['rows']), input_files=len(bundle['files']),
                    frozen_sha256=FROZEN,
                    exclusions=['Engine raw-link history/49-link cap and mixed static/dynamic collector order.',
                                'Full live building/construction/mine/ward/Way Gate and missile activity.',
                                'Full layered bridge and outside-map item-constructor admission.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.binary)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('status', 'cases', 'composed_scenarios', 'live_consumer_checks')}))


if __name__ == '__main__':
    main()
