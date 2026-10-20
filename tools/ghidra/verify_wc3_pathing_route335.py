#!/usr/bin/env python3
"""Verify ROUTE-04.1 public ground/flight cache lifetimes against frozen native repeats."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'tools/ghidra/fixtures'
HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def canonical(rows):
    pointers = {}
    def walk(value, key=None):
        if isinstance(value, dict):
            return {k: walk(v, k) for k, v in value.items()}
        if isinstance(value, list):
            return [walk(v) for v in value]
        if key in ('id', 'target') and isinstance(value, str) and re.fullmatch(r'0x[0-9a-f]+', value):
            return value if value == '0x0' else pointers.setdefault(value, 'pointer' + str(len(pointers)))
        return value
    return [walk(r) for r in rows if 'seq' in r]


def markers(text, prefix):
    return re.findall(re.escape(prefix) + r'[^"\r\n]*', text)


def expected_rows(rows):
    motion, cache = [], []
    last = None
    for row in rows:
        if row['event'] == 'advance-leave':
            last = row
        if row['event'] != 'commit-leave':
            continue
        require(len(row['g']['members']) == 1, 'unexpected group size')
        m = row['g']['members'][0]['m']
        motion.append([row['scene'], *m['clock'], *m['pos'], *m['vel'], m['heading']])
        if last and (last['scene'], last['c']) == (row['scene'], row['c']):
            p = last['path']
            cache.append([1, p['count'], p['index'] & 0xffffffff,
                          p['adaptiveCount'], p['adaptiveIndex'] & 0xffffffff])
        else:
            cache.append([0] * 5)
    return motion, cache


def header_rows(text, name):
    body = re.search(r'\b' + name + r'\[\]\[\d+\]=\{(.*?)\n\};', text, re.S)
    require(body is not None, 'missing engine fixture: ' + name)
    return [[int(word, 16) for word in re.findall(r'0x([0-9a-f]{8})u', line)]
            for line in body[1].splitlines() if '{' in line]


def validate(spec, bundle, root=ROOT):
    require(spec['task'] == 'ROUTE-04.1' and spec['binary_sha256'] == HASH, 'identity differs')
    require(set(spec['scenes']) == set(bundle['scenes']) == {'335', '336'}, 'missing scene')
    for path, digest in spec['sources'].items():
        require(sha((root / path).read_bytes()) == digest, 'source changed: ' + path)
    report = {}
    for number, scene in bundle['scenes'].items():
        reference = spec['scenes'][number]
        meta = json.loads(scene['map_metadata'])
        require(meta['task'] == 'ROUTE-04.1' and meta['map_sha256'] == reference['map_sha256'], 'map identity differs')
        require(sha(scene['map_metadata'].encode()) == reference['metadata_sha256'], 'map metadata changed')
        producer = meta
        if 'rebuild_metadata' in scene:
            require(sha(scene['captured_builder'].encode()) == meta['target_builder_sha256'], 'captured builder differs')
            require(sha(scene['rebuild_metadata'].encode()) == reference['rebuild_metadata_sha256'], 'rebuild metadata differs')
            producer = json.loads(scene['rebuild_metadata'])
            require(dict(meta, target_builder_sha256=producer['target_builder_sha256']) == producer,
                    'rebuilt map changed beyond builder provenance')
        for field, source in [('probe_sha256', 'probe.j'), ('target_builder_sha256', 'make_map.py')]:
            path = f'tools/frida/research/route{number}_{source}'
            require(producer[field] == spec['sources'][path], 'map producer differs: ' + path)
        runs = {}
        public = []
        for name in ('observe1', 'observe2', 'control'):
            run = scene[name]
            for field in ('trace', 'preload'):
                require(sha(run[field].encode()) == reference[name][field], 'run bytes changed')
            rows = [json.loads(line) for line in run['trace'].splitlines()]
            first, end = rows[0], rows[-1]
            require(first['event'] == 'metadata' and first['sha256'] == HASH and first['owned'], 'unowned/wrong binary')
            require(first['source_sha256']['map'] == meta['map_sha256'] and first['prefix'] == f'R{number} ', 'wrong loaded map')
            require(first['source_sha256']['separation323_capture.py'] == spec['sources']['tools/frida/research/separation323_capture.py'], 'capture producer differs')
            require(first['mode'] == ('control' if name == 'control' else 'observe'), 'wrong capture mode')
            require(first['env'] in ('B', 'C') and first['seconds'] == 260, 'unbounded/unowned capture')
            require(end['event'] == 'preload-file' and end['complete'], 'incomplete capture')
            require(end['sha256'] == sha(run['preload'].encode()), 'preload hash differs')
            require(not any(r['event'] in ('error', 'timeout', 'crash') for r in rows), 'capture error')
            inputs = [r for r in rows if r['event'] == 'owned-loading-input']
            require(inputs and all(r['rc'] == 0 and r['pid'] == first['pid'] for r in inputs), 'loading input failed')
            values = markers(run['preload'], f'R{number} ')
            require(len(values) == reference['markers'] and 'scene=16' in values[-1] and 'label=complete' in values[-1], 'public completion differs')
            public.append(values)
            if name == 'control':
                require(not any('seq' in r or r['event'] == 'module' for r in rows), 'control attached hooks')
            else:
                observer = f'tools/frida/research/route{number}_observer.js'
                require(first['source_sha256'][Path(observer).name] == spec['sources'][observer], 'observer changed')
                observed = [r['value'] for r in rows if r['event'] == 'marker' and 'label=start ' not in r['value']]
                require(observed == values, 'observer public stream differs')
                runs[name] = rows
        require(public[0] == public[1] == public[2], 'observer/control differs')
        require(canonical(runs['observe1']) == canonical(runs['observe2']), 'native semantic repeat differs')
        rows = runs['observe1']
        counts = Counter(r['event'] for r in rows if 'seq' in r and r['event'] != 'trace-end')
        require(dict(counts) == reference['events'], 'native event counts differ')
        ends = [r for r in rows if r['event'] == 'trace-end']
        require(len(ends) == 1 and ends[0]['installed'] and ends[0]['readOnly'] and
                ends[0]['counts'] == dict(counts) and ends[0]['seq'] == sum(counts.values()), 'observer completion differs')
        for kind in ('advance', 'fine', 'index', 'commit', 'recover', 'stop'):
            require(counts[kind + '-enter'] == counts[kind + '-leave'], 'unbalanced native calls')
        for row in rows:
            if row['event'] == 'index-leave':
                mode, p = row['mode'], row['path']
                require(mode in (0, 1), 'unobserved initialization mode')
                count = p['count'] if mode == 0 else p['adaptiveCount']
                index = p['index'] if mode == 0 else p['adaptiveIndex']
                require(count > 0 and index == (max(0, count - 2) if mode == 0 else count - 1), 'native initialization differs')
        motion, cache = expected_rows(rows)
        header = (root / f'games/warcraft-3/game/tests/fixtures/retail_route{number}.h').read_text()
        require(header_rows(header, f'route{number}_motion') == motion, 'engine motion fixture differs')
        require(header_rows(header, f'route{number}_cache') == cache, 'engine cache fixture differs')
        body = re.search(r'static char const route' + number + r'_script\[\]=(.*?);', header, re.S)
        require(body is not None, 'engine script missing')
        script = ''.join(json.loads(line.strip()) for line in body[1].splitlines() if line.strip())
        probe = (root / f'tools/frida/research/route{number}_probe.j').read_text()
        helper = 'function ModuloInteger takes integer dividend, integer divisor returns integer\nreturn dividend-(dividend/divisor)*divisor\nendfunction\n'
        expected_script = probe.replace('function R' + number + 'Mark', helper + 'function R' + number + 'Mark', 1) + '\nfunction main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n'
        require(script == expected_script, 'engine public script differs')
        report[number] = dict(native_events=sum(counts.values()), markers=len(public[0]),
                              motion=len(motion), cache=sum(row[0] for row in cache))
    return dict(status='verified', passed=True, binary_sha256=HASH, task='ROUTE-04.1', scenes=report)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    require(sha(args.binary.read_bytes()) == HASH, 'binary changed')
    spec = json.loads((FIXTURES / 'retail-route335-1.27.json').read_text())
    bundle = json.loads(gzip.decompress((FIXTURES / 'retail-route335-inputs-1.27.json.gz').read_bytes()))
    result = validate(spec, bundle)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
