#!/usr/bin/env python3
"""Audit two read-only public portal captures against an observer-free control."""
import argparse
import json
from pathlib import Path

from follow187_engine_fixture import capture


def portal_rows(rows):
    portals = [r for r in rows if r.get('event', '').startswith('portal-')]
    if [r['event'] for r in portals] != ['portal-enter', 'portal-held', 'portal-candidate', 'portal-leave']:
        raise ValueError('incomplete portal scope')
    enter, held, candidate, leave = portals
    if enter['self'] == '0x0' or any(r['self'] != enter['self'] for r in portals):
        raise ValueError('captured self identity changed')
    if enter['before'] != 0 or held['flags'] != 1 or candidate['flags'] != 1 or leave['after'] != 0:
        raise ValueError('portal exclusion was not held and restored')
    if held['callback'] != '0x0' or held['context'] != '0x0' or candidate['outer'] != '0x0':
        raise ValueError('portal acquired an outer callback')
    if leave['result'] != 1 or enter['args'] != [32, 24, 6]:
        raise ValueError('public portal admission differs')
    # Only allocation identity and observer wall time vary between processes.
    normalized = []
    for row in portals:
        result = {k: v for k, v in row.items() if k != 'ms'}
        result['self'] = 1
        for key in ('callback', 'context', 'outer'):
            if key in result:
                result[key] = 0
        normalized.append(result)
    return normalized


def verify(expected, archive):
    observations, public = [], []
    for name, pin in expected['captures'].items():
        rows, markers, mode = capture(archive / name, pin, expected['binary_sha256'], 'P188 ', 166)
        public.append(markers)
        if mode == 'observe':
            observations.append(portal_rows(rows))
    if len(observations) != 2 or any(r != expected['portal_rows'] for r in observations):
        raise ValueError('retail portal repeats differ')
    if len(public) != 3 or any(p != public[0] for p in public):
        raise ValueError('public repeats/control differ')
    rejected = []
    for name, pin in expected['rejected_captures'].items():
        _, markers, mode = capture(archive / name, pin, expected['binary_sha256'], 'P188 ', 166)
        rejected.append((mode, markers))
    observed = [m for mode, m in rejected if mode == 'observe']
    controls = [m for mode, m in rejected if mode == 'control']
    if len(observed) != 2 or len(controls) != 1 or observed[0] != observed[1]:
        raise ValueError('rejected broad observer evidence incomplete')
    differences = sum(a != b for a, b in zip(observed[0], controls[0]))
    if differences != expected['rejected_public_differences'] or differences == 0:
        raise ValueError('broad observer control rejection changed')
    return dict(status='live-portal-exclusion', passed=True, portal_requests=1,
                candidate_checks=1, public_markers=166, observations=2, controls=1,
                rejected_public_differences=differences)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    try:
        report = verify(json.loads(args.expected.read_text()), args.archive)
    except (ValueError, KeyError, OSError) as error:
        report = dict(status='failed', passed=False, error=str(error))
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
