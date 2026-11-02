#!/usr/bin/env python3
"""Verify original saved-rectangle insertion and the unchanged MAP-06.2 captures.

Execute 14d380, the insertion stage called by SpatialObject_Load, using original
owner/map/object constructors. Stream decoding is outside this isolated oracle;
saved rectangles/refcounts are supplied inputs. No game instructions are stubbed.
Full retained live captures certify the complete retail UI save/load operation.
"""
import argparse
import gzip
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / 'fixtures'
BINARY_SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def original(binary):
    sys.path.insert(0, str(HERE / 'research'))
    import sep03_map05_spatial_harness as H
    emu = H.Emu(binary)
    cases = []
    for rect in [(2, 2, 4, 4), (-1, -1, 2, 2), (6, 6, 9, 9), (-1, 6, 2, 9)]:
        for order in itertools.permutations(range(3)):
            world = H.World(emu, 8, 8)
            movers = [world.make_mover() for _ in range(3)]
            objects = [world.create_object(m) for m in movers]
            for obj in objects:
                world.update(obj, rect)
            # An actual original producer changes movement-era chain order.
            away = tuple(x + 3 for x in rect)
            world.update(objects[0], away)
            world.update(objects[0], rect)
            world.compact_all()
            before = {str(c): [objects.index(p) for _, _, p in world.chain(c)]
                      for c in range(64) if world.chain(c)}
            saved = [(i, emu.r(objects[i] + 0x1c, 4), emu.r(objects[i] + 0x3c)) for i in order]
            loaded = H.World(emu, 8, 8)
            new_movers = [loaded.make_mover() for _ in range(3)]
            new_objects = [loaded.create_object(m) for m in new_movers]
            for i, words, refs in saved:
                obj = new_objects[i]
                # The stream decoder restores these logical values before
                # executing the original insertion stage (14d000 -> 14d380).
                emu.w(obj + 0x1c, *words)
                emu.w(obj + 0x3c, refs)
                rectangle = emu.fixture(16)
                emu.w(rectangle, *words)
                emu.call(0x6f14d380, loaded.map, rectangle, obj)
                assert emu.esp_after == 12
            after = {str(c): [new_objects.index(p) for _, _, p in loaded.chain(c)]
                     for c in range(64) if loaded.chain(c)}
            assert set(after) == set(before)
            assert all(v == list(reversed(order)) for v in after.values())
            query = [new_objects.index(m) for m in loaded.query((0, 0, 8, 8))]
            assert query == list(reversed(order))
            assert not loaded.dirty()
            assert all(emu.r(o + 0x3c) == refs for i, _, refs in saved for o in [new_objects[i]])
            cases.append(dict(rectangle=list(rect), save_order=list(order), before=before, after=after,
                              candidates=query, records=loaded.fields()['records']))
    return dict(binary_sha256=BINARY_SHA, cases=cases)


def captures():
    data = json.loads(gzip.decompress((FIXTURES / 'retail-spatial-save-inputs-1.27.json.gz').read_bytes()))
    expected_path = FIXTURES / 'research/MAP-06.2-expected.json'
    expected = json.loads(expected_path.read_text())
    spec = importlib.util.spec_from_file_location('spatial_save_composer',
                    HERE.parents[0] / 'frida/research/sep03_map06_expected.py')
    composer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(composer)
    names = list(expected['captures'])
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name, text in data['files'].items():
            if Path(name).name != name:
                raise ValueError('unsafe capture filename')
            raw = text.encode()
            if hashlib.sha256(raw).hexdigest() != data['sha256'][name]:
                raise ValueError('retained capture differs: ' + name)
            (root / name).write_bytes(raw)
        for name, digest in expected['captures'].items():
            if data['sha256'].get(name) != digest:
                raise ValueError('frozen capture identity differs: ' + name)
        rebuilt = root / 'rebuilt.json'
        composer.map062(*(root / name for name in names), rebuilt)
        if rebuilt.read_bytes() != expected_path.read_bytes():
            raise ValueError('complete frozen retail save/load report differs')
    active = expected['active_route_ui_save_load']
    matched = {r['matched']: r for r in active['resumed'].values() if r['matched']}
    assert set(matched) == {'M', 'C0', 'C1', 'C2'}
    assert all(r['equal'] and r['first_difference'] is None for r in matched.values())
    assert all(r['equal'] for r in expected['scripted_save_continue'].values())
    assert all(active['presave_prefix_equal'].values())
    for state in (active['chains'], expected['idle_ui_load']):
        assert state['save_order_equals_load_order'] and state['after_load_chains_descending_save_index']
    markers = expected['jass_markers']
    assert all(markers['first_pass_equal_control'].values())
    assert all(markers['resumed_equal_control_suffix'].values())
    # Whole files differ because UI saves landed on different ticks. Retain
    # this false result; only each matched control suffix certifies parity.
    assert markers['saveload_control_equals_observe'] is False
    for name in names[:6]:
        rows = [json.loads(line) for line in data['files'][name].splitlines() if line.strip()]
        ends = [r for r in rows if r.get('event') == 'trace-end']
        assert len(ends) == 1 and ends[0]['installed'] and not ends[0]['caps']
        assert not [r for r in rows if r.get('event') in ('error', 'observer-error')]
    return dict(captured_files=len(data['files']), matched_movers=len(matched),
                resumed_commits=sum(r['loaded_commits'] for r in matched.values()),
                frozen_sha256=hashlib.sha256(expected_path.read_bytes()).hexdigest())


def verify(binary, report):
    observed = original(binary)
    frozen = json.loads((FIXTURES / 'retail-spatial-load-insertion-1.27.json').read_text())
    if observed != frozen:
        raise ValueError('original saved-rectangle insertion differs from frozen evidence')
    live = captures()
    result = dict(passed=True, binary_sha256=BINARY_SHA, insertion_cases=len(observed['cases']), **live)
    report.write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(verify(args.binary.resolve(), args.report.resolve()), indent=2))
