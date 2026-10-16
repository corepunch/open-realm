#!/usr/bin/env python3
"""Verify complete Holy Light/target Move repeat and observer-free captures.

Only process addresses and canonical identities are removed from comparison.
Fine poses, velocities, radii, ranges, route states and owner counters remain raw.
The map used here is the builder's --no-wall variant (dry 64x64 fine cells).
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
MARKER = re.compile(r'call Preload\( "(S184 [^"\r\n]*)" \)')
IDENTITIES = {'p', 'm', 'id', 'grp', 'ft', 'links'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def state(value):
    if isinstance(value, dict):
        return {k: state(v) for k, v in value.items() if k not in IDENTITIES}
    if isinstance(value, list):
        return [state(v) for v in value]
    return value


def extract(path):
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    meta = rows[0]
    if meta.get('event') != 'metadata' or meta.get('sha256') != HASH or not meta.get('owned'):
        raise ValueError('Unsupported or unowned capture')
    if any(r.get('event') in ('trace-failed', 'error') for r in rows):
        raise ValueError('Failed capture')
    preload = path.with_suffix('').with_name(path.stem + '-preload.txt')
    markers = MARKER.findall(preload.read_text())
    final = next((r for r in rows if r.get('event') == 'preload-file'), {})
    if len(markers) != 376 or ' label=complete' not in markers[-1] or not final.get('complete') or final.get('sha256') != digest(preload):
        raise ValueError('Incomplete or changed public simulation markers')
    result = dict(mode=meta['mode'], markers=markers, visits=[], tasks=[], motion=[])
    if meta['mode'] == 'control':
        if any(r.get('event') in ('gtick', 'module', 'trace-end') for r in rows):
            raise ValueError('Instrumented control')
        return result
    if not next((r for r in rows if r.get('event') == 'controller-end'), {}).get('complete'):
        raise ValueError('Incomplete observer')
    end = next((r for r in rows if r.get('event') == 'trace-end'), {})
    if not end.get('installed') or any(k.endswith('-dropped') and v for k, v in end.get('counts', {}).items()):
        raise ValueError('Missing or truncated observer')
    clocks = {r['c']: r['clock'] for r in rows if r.get('event') == 'spell-owner-clock' and r['phase'] == 'begin'}
    scene = -1
    source = None
    for r in rows:
        event = r.get('event')
        if event == 'marker' and 'label=begin-setup' in r['value']:
            scene = int(re.search(r' s=(-?\d+)', r['value'])[1])
        if event == 'begin-task':
            result['tasks'].append([scene, r['range'], r['persistent'], r['a3']])
        if event == 'begin-target' and scene == 0:
            source = r['self']
        if event != 'gtick':
            continue
        result['visits'].append([scene, r['c'], r['flags'], r['age'], r['comp'], r['cd'], r['unseen'],
                                 state(r['path']), state(r['members'])])
        member = next((m for m in r['members'] if m.get('id') == source), None)
        if scene == 0 and member and r['c'] in clocks:
            result['motion'].append([r['c'], *clocks[r['c']], *member['pos'], *member['vel'], member['range']])
    if len(result['visits']) != 591 or result['tasks'] != [[0, 0x44480000, 0, 1], [1, 0x43960000, 1, 1], [2, 0x44480000, 0, 1]]:
        raise ValueError('Incomplete or changed producer/owner stream')
    return result


def header(motion):
    if len(motion) != 50:
        raise ValueError('Incomplete primary-clock motion')
    return ('/* Raw retail Holy Light target approach; Payoff185.\n'
            ' * counter, primary clock, committed fine position/velocity, captured range. */\n'
            'static uint32_t const spell185_motion[][9]={\n' +
            ''.join('    {' + ','.join('0x%08xu' % v for v in row) + '},\n' for row in motion) + '};\n')


def verify(archive, expected, engine_header):
    captures = []
    for pin in expected['captures']:
        path = archive / pin['file']
        if digest(path) != pin['sha256'] or digest(path.with_name(path.stem + '-preload.txt')) != pin['preload_sha256']:
            raise ValueError('Capture provenance changed')
        captures.append(extract(path))
    observed = [r for r in captures if r['mode'] == 'observe']
    controls = [r for r in captures if r['mode'] == 'control']
    if len(observed) != 3 or len(controls) != 1:
        raise ValueError('Missing repeats or observer-free control')
    if any(r['markers'] != observed[0]['markers'] for r in captures):
        raise ValueError('Public repeat/control divergence')
    if any(r['visits'] != observed[0]['visits'] or r['tasks'] != observed[0]['tasks'] for r in observed):
        raise ValueError('Raw repeat divergence')
    if observed[1]['motion'] != observed[2]['motion'] or observed[1]['motion'] != expected['motion']:
        raise ValueError('Primary-clock motion divergence')
    if engine_header.read_text() != header(expected['motion']):
        raise ValueError('Engine fixture detached from retail')
    return dict(status='live-spell-approach', passed=True, captures=4, observed_repeats=3, controls=1,
                scenes=3, raw_owner_visits=591, motion_rows=50, public_markers=376,
                tasks=observed[0]['tasks'])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--archive', type=Path, required=True)
    ap.add_argument('--expected', type=Path, required=True)
    ap.add_argument('--header', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    result = verify(args.archive, json.loads(args.expected.read_text()), args.header)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
