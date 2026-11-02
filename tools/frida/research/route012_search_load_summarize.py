#!/usr/bin/env python3
"""Verify complete UI-load search resets and observer-free position controls."""
import argparse
import gzip
import hashlib
import json
import re
from pathlib import Path


def read(path):
    return gzip.decompress(path.read_bytes()) if path.suffix == '.gz' else path.read_bytes()


def summarize(captures, preloads):
    runs = []
    marker_runs = []
    for index, (capture, preload) in enumerate(zip(captures, preloads)):
        raw, public = read(capture), read(preload)
        rows = [json.loads(line) for line in raw.splitlines()]
        meta = rows[0]
        assert meta['event'] == 'metadata' and meta['owned']
        assert meta['mode'] == ('control' if index == 2 else 'observe')
        assert meta['sha256'] == 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
        assert not any(row.get('event') == 'trace-failed' or row.get('type') == 'error' for row in rows)
        end = next(row for row in rows if row.get('event') == ('control-end' if index == 2 else 'trace-end'))
        assert end['elapsed'] >= meta['seconds']
        artifacts = rows[-1]
        assert artifacts['event'] == 'artifacts' and not artifacts['errors']
        assert artifacts['preload_sha256']['rs-ui_route.txt'] == hashlib.sha256(public).hexdigest()
        markers = re.findall(rb'call Preload\( "((?:RSPATIAL|RSCROWD) [^"\r\n]*)" \)', public)
        assert any(b'tick=300 label=complete' in marker for marker in markers), 'incomplete map'
        # UI actions occur on wall time, so a different saved tick changes the
        # number of repeated samples. Every repeated (tick,label) must agree;
        # compare the full unique coordinate sequence through completion.
        samples = {}
        for marker in markers:
            parts = re.split(rb' (?=x=|c0=)', marker, maxsplit=1)
            if len(parts) != 2:
                continue  # init/before-unit markers carry no position sample
            key, value = parts
            assert key not in samples or samples[key] == value, 'loaded position differs from its uninterrupted tick'
            samples[key] = value
        marker_runs.append(sorted(samples.items()))
        state = None
        if index < 2:
            constructors = [row for row in rows if row.get('event') == 'search-owner-construct']
            loaded = [row for row in rows if row.get('event') == 'search-owners-after-load']
            assert len(constructors) == 4 and len(loaded) == 1
            expected = dict(source=0xffffffff, nearest=0xffffffff, count=0, budget=0)
            for row in constructors:
                assert {key: row[key] for key in expected} == expected
            state = {kind: {key: loaded[0][kind][key] for key in expected} for kind in ('fine', 'coarse')}
            assert state == dict(fine=expected, coarse=expected)
            loads = [row for row in rows if row.get('event') == 'search-owner-load']
            assert len(loads) == 2 and {row['coarse'] for row in loads} == {False, True}
            assert not any(row.get('event') == 'search-owner-construct' and row['generation'] > 0 for row in rows)
        else:
            assert not any(str(row.get('event', '')).startswith('search-owner') for row in rows)
        runs.append(dict(mode=meta['mode'], map_sha256=meta['source_sha256']['map'],
                         capture_sha256=hashlib.sha256(raw).hexdigest(),
                         preload_sha256=hashlib.sha256(public).hexdigest(), markers=len(markers),
                         coordinate_samples=len(samples), loaded=state))
    assert len(runs) == 3 and len({row['map_sha256'] for row in runs}) == 1
    assert marker_runs[0] == marker_runs[1] == marker_runs[2], 'observer/UI-load position sequences differ'
    return dict(runs=runs, observer_repeats=2, observer_free_controls=1,
                public_markers=len(marker_runs[0]), full_public_sequences_equal=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--captures', type=Path, nargs=3, required=True)
    parser.add_argument('--preloads', type=Path, nargs=3, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--expected', type=Path)
    args = parser.parse_args()
    result = summarize(args.captures, args.preloads)
    if args.expected:
        assert result == json.loads(args.expected.read_text())
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(public_markers=result['public_markers'], full_public_sequences_equal=True)))


if __name__ == '__main__':
    main()
