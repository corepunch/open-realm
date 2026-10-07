#!/usr/bin/env python3
"""Verify original object-category inventory and complete controlled retail captures.

Compare production cell traversal with original code and the independent model.
Actual engine publication/admission/save behavior is covered by game regressions.
"""
import argparse
import ctypes
import gzip
import hashlib
import importlib.util
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


def cell_library(root, optimization):
    output = root / ('cell-' + optimization + '.so')
    subprocess.run(['cc', '-' + optimization, '-shared', '-fPIC',
                    str(HERE / 'wc3_cell_query_probe.c'), '-o', str(output)], check=True)
    library = ctypes.CDLL(str(output))
    word = ctypes.c_uint32
    pointer = ctypes.POINTER(word)
    library.cell_query.argtypes = [word, word, word, word, pointer, pointer, word, pointer, word, word, pointer]
    library.cell_query.restype = None
    return library


def cell_result(library, pred, links, objects, query, mode=0, target=None, terrain=0):
    word = ctypes.c_uint32
    kinds = (word * len(links))(*(k for k, _ in links))
    identities = (word * len(links))(*(o if o is not None else 0 for _, o in links))
    attributes = []
    for obj in objects:
        attributes.extend([obj.cat | (0x1000000 if obj.active else 0),
                           {'live': 0, 'dead': 0xffffffff, 'collide': 1001}[obj.live], obj.flags, obj.mover])
    out = (word * 40)()
    library.cell_query(('fine', 'hier', 'collect', 'union').index(pred), query, mode,
                       0xffffff if target is None else target, kinds, identities, len(links),
                       (word * len(attributes))(*attributes), len(objects), terrain, out)
    if pred == 'fine':
        result = dict(clear=out[0], target_seen=out[1], blocked_flag=int(not out[0]))
    elif pred == 'hier':
        result = dict(clear=out[0])
    elif pred == 'union':
        result = dict(union=out[0])
    else:
        result = dict(tokens=[('null', None) if t == 0xffffffff else ('mover', t) for t in out[8:8 + out[2]]])
    return result, list(out[3:4 + len(objects)])


def verify_cell_saved_evidence(payload=None):
    if payload is None:
        payload = json.loads((FIXTURES / 'retail-cell-consumers-ghidra-1.27.json').read_text())
    expected = {'6f1489a0', '6f148e90', '6f148ad0', '6f149170', '6f1493d0', '6f15cf80', '6f15d0e0'}
    if payload['binary_sha256'] != SHA or payload['unsaved'] or len(payload['rows']) != len(expected):
        raise ValueError('cell-consumer Ghidra evidence is not saved for this target')
    if {row['address'] for row in payload['rows']} != expected or any(
            'Payoff137' not in row['comment'] or 'Payoff137' not in row['mapper_comment'] for row in payload['rows']):
        raise ValueError('cell-consumer function inventory differs')
    return len(expected)


def verify_cell_consumers(binary, root):
    sys.path.insert(0, str(HERE / 'research'))
    spec = importlib.util.spec_from_file_location('foot031', HERE / 'research/verify_FOOT-03.1_eligibility.py')
    research = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(research)
    oracle = research.Oracle(binary)
    libraries = [cell_library(root, optimization) for optimization in ('O0', 'O2')]
    hashes = [hashlib.sha256(), hashlib.sha256()]
    original_run = oracle.run

    def paired(pred, links, objects, query, mode=0, target=None, **arguments):
        result = original_run(pred, links, objects, query, mode, target, **arguments)
        stamps = [oracle.e.r(oracle.r.map + 0xb4)] + [oracle.e.r(obj + 0x38) for obj, _ in oracle.pool[:len(objects)]]
        for library, fingerprint in zip(libraries, hashes):
            actual, actual_stamps = cell_result(library, pred, links, objects, query, mode, target)
            if actual != result or actual_stamps != stamps:
                raise ValueError('production cell differs: ' + repr((pred, links, query, mode, actual, result,
                                                                    actual_stamps, stamps)))
            fingerprint.update(json.dumps([actual, actual_stamps], sort_keys=True).encode())
        return result

    oracle.run = paired
    single = research.single_link_matrix(oracle)
    double = research.two_record_matrix(oracle)
    cap = research.link_cap(oracle)
    composed = research.composed(oracle)
    terrain_cases = 0
    for pred in ('fine', 'hier', 'collect', 'union'):
        for terrain in (0, 0x02000000, 0x04000000):
            for query in (0, 0x02000002, 0x04000004):
                objects = [research.Obj(0xca, flags=0x10000000)]
                addresses = oracle.build([(1, 0)], objects)
                oracle.e.w(oracle.r.cells + 4 * (5 * oracle.r.width + 5),
                           oracle.e.r(oracle.r.cells + 4 * (5 * oracle.r.width + 5)) | terrain)
                oracle.e.w(oracle.r.map + 0xb4, research.STAMP)
                if pred == 'fine':
                    result = oracle.r.fine_cell(5, 5, query, 0, addresses[0])
                elif pred == 'hier':
                    result = dict(clear=oracle.r.hier_cell(5, 5, query))
                elif pred == 'collect':
                    tokens = oracle.r.collect(5, 5, query)
                    result = dict(tokens=[('null', None) if t == 0 else ('mover', 0) for t in tokens])
                else:
                    oracle.e.w(oracle.r.sys + 0xa4, query)
                    result = dict(union=oracle.e.call(0x6f149170, oracle.r.sys, 5, 5))
                    assert oracle.e.esp_after == 12
                stamps = [oracle.e.r(oracle.r.map + 0xb4), oracle.e.r(addresses[0] + 0x38)]
                for library in libraries:
                    actual, actual_stamps = cell_result(library, pred, [(1, 0)], objects, query, target=0, terrain=terrain)
                    if actual != result or actual_stamps != stamps:
                        raise ValueError('terrain-first cell differs: ' + repr((pred, terrain, query, result, actual, stamps, actual_stamps)))
                terrain_cases += 1
    oracle.r.reset_map()
    corners = [(8, 8), (9, 8), (9, 9), (8, 9)]
    objects = oracle.install([research.Obj(0xca, flags=0x10000000) for _ in corners])
    for (x, y), obj in zip(corners, objects):
        oracle.r.record(x, y, obj, True)
    cell = oracle.r.cells + 4 * (8 * oracle.r.width + 9)
    oracle.e.w(cell, oracle.e.r(cell) | 0x80000000)
    oracle.e.w(oracle.r.map + 0xb4, 1000)
    oracle.r.rebuild_hierarchy()
    order = dict(final_stamp=oracle.e.r(oracle.r.map + 0xb4),
                 object_stamps=[oracle.e.r(obj + 0x38) for obj in objects], classes=oracle.r.hier_class(8, 8))
    frozen_order = json.loads((FIXTURES / 'retail-cell-hierarchy-order-1.27.json').read_text())
    if any(order[key] != frozen_order[key] for key in order):
        raise ValueError('original hierarchy stamp order differs')
    if hashes[0].digest() != hashes[1].digest():
        raise ValueError('optimization changes cell results')
    return dict(cases=oracle.cases, single_link_cases=single, chain_cases=double,
                mismatches=oracle.mismatches, link_cap=cap, composed=composed,
                engine_cell_cases_per_build=oracle.cases, engine_optimizations=['O0', 'O2'],
                engine_cell_sha256=hashes[0].hexdigest(), engine_terrain_cases_per_build=terrain_cases,
                hierarchy_order=order)



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
        eligibility = verify_cell_consumers(binary, root)
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
        cell_saved = verify_cell_saved_evidence()
        return dict(binary_sha256=SHA, passed=True, status='verified', differences=[],
                    cases=50112, composed_scenarios=16, live_captures=2,
                    live_consumer_checks=sum(s['consumer_checks'] for s in live), control_markers=60,
                    saved_functions=len(ghidra['rows']), input_files=len(bundle['files']),
                    frozen_sha256=FROZEN,
                    engine_cell_cases_per_build=eligibility['engine_cell_cases_per_build'],
                    engine_optimizations=eligibility['engine_optimizations'], engine_cell_sha256=eligibility['engine_cell_sha256'],
                    engine_terrain_cases_per_build=eligibility['engine_terrain_cases_per_build'],
                    cell_saved_functions=cell_saved,
                    hierarchy_order=eligibility['hierarchy_order'],
                    exclusions=['Engine static-region producers and mixed static/dynamic reference-count lifecycles (FOOT-03.2).',
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
