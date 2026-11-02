#!/usr/bin/env python3
"""Verify repeated public arrivals and execute the original queued-arrival matrix."""
import argparse
from collections import Counter
import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from verify_wc3_pathing_route335 import ROOT, FIXTURES, HASH, canonical, markers, require, sha


def validate(spec, bundle):
    require(sha(json.dumps(bundle['oracle'], sort_keys=True).encode()) == spec['oracle_sha256'],
            'oracle reference changed')
    require(len(bundle['oracle']['rows']) == 1536, 'incomplete oracle matrix')
    control = bundle['control']
    control_rows = [json.loads(s) for s in control['trace'].splitlines()]
    require(control_rows[-1].get('complete') is True, 'incomplete control')
    require(not any('seq' in r or r['event'] == 'module' for r in control_rows), 'control attached hooks')
    public = markers(control['preload'], 'R335 ')
    require(len(public) == 1329, 'incomplete public stream')
    runs = []
    for name in ('observe1', 'observe2'):
        run = bundle['runs'][name]
        rows = [json.loads(s) for s in run['trace'].splitlines()]
        require(rows[-1].get('complete') is True, 'incomplete capture')
        meta = rows[0]
        require(meta['sha256'] == HASH and meta['source_sha256']['map'] == spec['map_sha256'], 'native input changed')
        require(meta['source_sha256']['route337_observer.js'] == spec['sources']['tools/frida/research/route337_observer.js'], 'observer changed')
        require(markers(run['preload'], 'R335 ') == public, 'public control differs')
        require([r['value'] for r in rows if r['event'] == 'marker' and 'label=start ' not in r['value']] == public, 'observed public stream differs')
        counts = Counter(r['event'] for r in rows if 'seq' in r and r['event'] != 'trace-end')
        ends = [r for r in rows if r['event'] == 'trace-end']
        require(len(ends) == 1 and ends[0]['readOnly'] and ends[0]['installed'] and ends[0]['counts'] == counts and ends[0]['seq'] == sum(counts.values()), 'observer completion differs')
        for kind in ('step', 'unlink', 'advance', 'fine', 'commit', 'stop', 'recover', 'index'):
            require(counts[kind+'-enter'] == counts[kind+'-leave'], 'unbalanced calls')
        stack, arrivals = [], 0
        for row in rows:
            if row['event'] == 'step-enter':
                stack.append([row, []])
            elif row['event'] == 'unlink-enter' and stack:
                stack[-1][1].append(row)
            elif row['event'] == 'step-leave':
                require(bool(stack), 'missing step entry')
                before, unlinks = stack.pop()
                if not row['outputs'][2]:
                    continue
                arrivals += 1
                require(len(unlinks) == 1 and unlinks[0]['caller'] == '0x16fc47', 'arrival unlink missing')
                expected = dict(before['path'])
                expected['flags'] &= ~0x02000000
                require(row['path'] == expected, 'arrival changed retained route')
                require(row['links'] == [0, 0] and not row['m']['flags'] & 0x10000, 'arrival did not retire request/force')
                require(row['outputs'][0] == 0 and row['outputs'][4] == 0, 'arrival output differs')
        require(not stack and arrivals == 25, 'incomplete arrival coverage')
        require(sum(counts.values()) == 18600, 'native event coverage changed')
        runs.append(canonical(rows))
    require(runs[0] == runs[1], 'semantic repeat differs')
    return dict(native_events=18600, arrivals=25, markers=1329, oracle_cases=1536)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    require(sha(args.binary.read_bytes()) == HASH, 'binary changed')
    spec = json.loads((FIXTURES/'retail-route337-1.27.json').read_text())
    data = gzip.decompress((FIXTURES/'retail-route337-inputs-1.27.json.gz').read_bytes())
    require(sha(data) == spec['bundle_sha256'], 'bundle changed')
    for path, digest in spec['sources'].items():
        require(sha((ROOT/path).read_bytes()) == digest, 'source changed: '+path)
    bundle = json.loads(data)
    result = validate(spec, bundle)
    with tempfile.TemporaryDirectory(prefix='route337-') as temp:
        reference, report = Path(temp)/'reference.json', Path(temp)/'report.json'
        reference.write_text(json.dumps(bundle['oracle']))
        subprocess.run([sys.executable, str(ROOT/'tools/ghidra/research/route337_arrival.py'),
                        '--binary', str(args.binary), '--reference', str(reference), '--report', str(report)], check=True)
        require(json.loads(report.read_text())['passed'], 'original oracle failed')
    result.update(status='verified', passed=True, binary_sha256=HASH)
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
