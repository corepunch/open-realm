#!/usr/bin/env python3
"""ORDER-01.12 Harvest ownership: frozen retail H012/I012 captures and replay model.

The bundle stores the complete public Preload marker streams of every owned
retail run (two read-only observed repeats plus one observer-free control per
map) and the observed user-order admission/append records. `verify` rehashes
every member, requires identical public streams across repeats and controls,
re-derives the ownership transition model from the raw markers, compares it
with the frozen expectation and, with --binary, pins the original instruction
bytes of the publishers this model depends on.
"""
import argparse, base64, gzip, hashlib, json, re, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BINARY_SHA256 = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
# Original function bodies/branches [start, end) the ownership model relies on.
GUARD_RANGES = {
    '6f438410': '6f4384ea',  # mining completion / carried arrival appends Resume d0031
    '6f438880': '6f438abd',  # shared deposit: explicit Return completes, else Harvest d0032
    '6f437fb0': '6f438184',  # lumber task completion: full load appends Resume d0031
    '6f687f60': '6f687fff',  # pending user successor test
    '6f680320': '6f6803a1',  # Unit_AdmitOrder Unit198>0 retention branch (680351)
}
MARKER = re.compile(r'call Preload\( "([HI]012 [^"\r\n]*)" \)')
FIELD = re.compile(r'(\w+)=(\S+)')
RUNS = {'H012': ('observe1', 'observe2', 'control1'), 'I012': ('b-observe1', 'b-observe2', 'b-control1')}


def markers(raw):
    return MARKER.findall(raw.decode('utf-8', 'replace'))


def model(stream, prefix):
    """Ordered public ownership transitions per scene: every non-sample marker and
    every sample where the worker head, visibility or resources change."""
    scenes, previous = {}, {}
    for value in stream:
        fields = dict(FIELD.findall(value))
        if 'order' not in fields:
            continue
        scene = int(fields['scene'])
        state = (int(fields['order']), fields['hidden'], fields['gold'], fields.get('lumber', ''), fields['mine'])
        if fields['label'] != 'sample' or previous.get(scene) != state:
            tick = int(fields['local'] if prefix == 'I012' else int(fields['tick']) % 240)
            scenes.setdefault(str(scene), []).append([tick, fields['label'], state[0], state[1] == '1'])
        previous[scene] = state
    return scenes


def admissions(rows):
    """Observed user-order admission/append records reduced to stable facts."""
    out = []
    for row in rows:
        if row.get('event') in ('admit-enter', 'append-enter'):
            order = row.get('order') or {}
            out.append([row['scene'], row['tick'], row['event'], order.get('command'), row.get('mode'), row['caller'], row['unit']['count']])
    return out


def binary_guards(binary, guards):
    raw = binary.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == BINARY_SHA256, 'unsupported game.dll'
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    assert raw[pe:pe + 4] == b'PE\0\0'
    count = struct.unpack_from('<H', raw, pe + 6)[0]
    optsize = struct.unpack_from('<H', raw, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from('<I', raw, opt + 28)[0]
    sections = [struct.unpack_from('<IIII', raw, opt + optsize + i * 40 + 8) for i in range(count)]
    for address, value in guards.items():
        want = bytes.fromhex(value)
        rva = int(address, 16) - base
        section = next((s for s in sections if s[1] <= rva and rva + len(want) <= s[1] + s[2]), None)
        assert section, address
        offset = section[3] + rva - section[1]
        assert raw[offset:offset + len(want)] == want, address


def read_guards(binary):
    raw = binary.read_bytes()
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    count = struct.unpack_from('<H', raw, pe + 6)[0]
    optsize = struct.unpack_from('<H', raw, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from('<I', raw, opt + 28)[0]
    sections = [struct.unpack_from('<IIII', raw, opt + optsize + i * 40 + 8) for i in range(count)]
    out = {}
    for start, end in GUARD_RANGES.items():
        rva = int(start, 16) - base
        section = next(s for s in sections if s[1] <= rva < s[1] + s[2])
        offset = section[3] + rva - section[1]
        out[start] = raw[offset:offset + int(end, 16) - int(start, 16)].hex()
    return out


def load(bundle_path):
    bundle = json.loads(gzip.decompress(bundle_path.read_bytes()))
    assert bundle['version'] == 1 and bundle['task'] == 'ORDER-01.12'
    files = {}
    for name, entry in bundle['files'].items():
        assert not Path(name).is_absolute() and '..' not in Path(name).parts
        raw = base64.b64decode(entry['base64'], validate=True)
        assert hashlib.sha256(raw).hexdigest() == entry['sha256'], name
        files[name] = raw
    return bundle, files


def verify(bundle_path, spec, binary=None):
    bundle, files = load(bundle_path)
    assert set(files) == {n for runs in RUNS.values() for r in runs for n in (r + '-preload.txt', r + '.jsonl')}
    result = {'maps': {}, 'public_markers': 0, 'controls': 0, 'observations': 0}
    for prefix, runs in RUNS.items():
        expected = spec['maps'][prefix]
        streams, observed = [], []
        for run in runs:
            rows = [json.loads(line) for line in files[run + '.jsonl'].decode().splitlines()]
            meta = rows[0]
            assert meta['event'] == 'metadata' and meta['owned'] is True and meta['sha256'] == BINARY_SHA256
            assert meta['remote'] in ('127.0.0.1:27049', '127.0.0.1:27050') and meta['display'] in (':98', ':99')
            assert meta['source_sha256']['map'] == expected['map_sha256']
            footer = rows[-1]
            assert footer['event'] == 'preload-file' and footer['complete'] is True
            stream = markers(files[run + '-preload.txt'])
            assert hashlib.sha256(files[run + '-preload.txt']).hexdigest() == footer['sha256']
            assert len(stream) == footer['markers'] == expected['markers']
            assert 'label=complete' in stream[-1]
            if run.endswith('control1'):
                assert meta['mode'] == 'control'
                assert all(row['event'] in ('metadata', 'resumed-before-attach', 'loading-key', 'owned-process-already-exited', 'preload-file') for row in rows), 'instrumented control'
                result['controls'] += 1
            else:
                assert meta['mode'] == 'observe'
                assert any(row['event'] == 'trace-end' and row['readOnly'] is True for row in rows)
                observed.append(admissions(rows))
                result['observations'] += 1
            streams.append(stream)
        assert all(s == streams[0] for s in streams), 'public streams differ between repeats/control'
        digest = hashlib.sha256('\n'.join(streams[0]).encode()).hexdigest()
        assert digest == expected['marker_sha256'], prefix
        derived = model(streams[0], prefix)
        assert derived == expected['ownership'], prefix + ' ownership model differs'
        assert all(o == observed[0] for o in observed), 'observer admissions differ between repeats'
        assert observed[0] == expected['admissions'], prefix + ' admissions differ'
        result['maps'][prefix] = {'markers': len(streams[0]), 'scenes': len(derived), 'transitions': sum(len(v) for v in derived.values()), 'admissions': len(observed[0])}
        result['public_markers'] += len(streams[0]) * len(runs)
    if binary is not None:
        binary_guards(binary, spec['instruction_guards'])
        result['guard_bytes'] = sum(len(bytes.fromhex(v)) for v in spec['instruction_guards'].values())
    return result


def freeze(capture_dir, output, spec_out, binary):
    """Archive the raw captures and write the derived expectation (research use only)."""
    files, maps = {}, {}
    for prefix, runs in RUNS.items():
        for run in runs:
            for name in (run + '-preload.txt', run + '.jsonl'):
                raw = (capture_dir / name).read_bytes()
                files[name] = {'sha256': hashlib.sha256(raw).hexdigest(), 'base64': base64.b64encode(raw).decode()}
        first = runs[0]
        stream = markers((capture_dir / (first + '-preload.txt')).read_bytes())
        rows = [json.loads(line) for line in (capture_dir / (first + '.jsonl')).read_text().splitlines()]
        maps[prefix] = {'map_sha256': rows[0]['source_sha256']['map'], 'markers': len(stream),
                        'marker_sha256': hashlib.sha256('\n'.join(stream).encode()).hexdigest(),
                        'ownership': model(stream, prefix), 'admissions': admissions(rows)}
    bundle = {'version': 1, 'task': 'ORDER-01.12', 'binary_sha256': BINARY_SHA256, 'files': files}
    output.write_bytes(gzip.compress(json.dumps(bundle, sort_keys=True).encode(), compresslevel=9, mtime=0))
    spec_out.write_text(json.dumps({'maps': maps, 'instruction_guards': read_guards(binary)}, indent=1, sort_keys=True) + '\n')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--freeze', type=Path, help='capture directory to archive')
    ap.add_argument('--bundle', type=Path, required=True)
    ap.add_argument('--spec', type=Path, required=True)
    ap.add_argument('--binary', type=Path)
    args = ap.parse_args()
    if args.freeze:
        freeze(args.freeze, args.bundle, args.spec, args.binary)
    else:
        print(json.dumps(verify(args.bundle, json.loads(args.spec.read_text()), args.binary)))
