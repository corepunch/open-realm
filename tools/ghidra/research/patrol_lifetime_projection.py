#!/usr/bin/env python3
"""Reproduce the complete historical Patrol B/C tick0..240 diagnostic.

--check never writes evidence. --output writes a canonical header to a NEW path.
There is no historical same-map unhooked control; new124c is separate evidence.
Arrival clocks are paired velocity clocks, not independent arrival timestamps.
"""
import argparse
from collections import Counter
import copy
import gzip
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]
FIXTURES = ROOT / 'tools/ghidra/fixtures'
HEADER = ROOT / 'games/warcraft-3/game/tests/fixtures/retail_patrol_lifetime_polls.h'
PROVENANCE = HEADER.with_suffix('.provenance.json')
PROBE = ROOT / 'tools/frida/wc3_patrol_lifetime_probe.j'
START = 'PATHTRACE tick=0 label=start_patrol_lifetime'
END = 'PATHTRACE tick=240 label=patrol_lifetime'
SCOPE = 'Repeated native diagnostic; same-map observer-free control absent. Full declared public lifetime0..240; all postcomplete tails retained in originalarchives.'
CLOCK = 'Arrival clock borrowed from immediately next same-mover velocitycommit; both records are asserted adjacent in original filtered globalstream. It is not separately sampled arrivalclock.'
ROLES = 'i0..6 literalprobe udg_PathPatrolSubjects[i], unique initialsource(272,256+256*i)'


def need(value, message):
    if not value:
        raise ValueError(message)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def project(records):
    starts = [i for i, x in enumerate(records) if x.get('value') == START]
    ends = [i for i, x in enumerate(records) if x.get('value') == END]
    need(len(starts) == len(ends) == 1 and starts[0] < ends[0], 'declared window')
    need(records[0]['event'] == 'metadata' and records[-1]['event'] == 'trace-end'
         and records[-1]['installed'], 'capture metadata/footer')
    counts = Counter(x.get('event') for x in records)
    for event in ('arrival-evaluation', 'velocity-commit'):
        need(counts[event] == records[-1]['counts'][event], 'footer event count')
    rows = [x for x in records[starts[0]:ends[0]]
            if x.get('event') in ('arrival-evaluation', 'velocity-commit')]
    need(len(rows) == 7124, 'entire window row count')
    roles, result = {}, []
    for i in range(0, len(rows), 2):
        ar, ve = rows[i:i+2]
        need(ar['event'] == 'arrival-evaluation' and ve['event'] == 'velocity-commit'
             and ar['mover'] == ve['mover'], 'global arrival/velocity pairing')
        mover = ar['mover']
        if mover not in roles:
            xy = [struct.unpack('<f', struct.pack('<I', word))[0] for word in ar['source']]
            need(xy[0] == 8.5 and xy[1] in (8, 16, 24, 32, 40, 48, 56), 'initial role pose')
            role = int(xy[1] / 8 - 1)
            need(role not in roles.values(), 'duplicate actor role')
            roles[mover] = role
        role, clock = roles[mover], ve['clock'][0]
        result.append(dict(phase=0, role=role, words=[clock, *ar['source'], *ar['destination'],
            ar['heading'], ar['threshold'], ar['angle'], ar['inRange'], ar['result']],
            flags=ar['flags'], footprint=ar['footprint'], storedRange=ar['storedRange'],
            storedPosition=ar['storedPosition']))
        result.append(dict(phase=2, role=role, words=[clock, *ve['after'][2:4], *ve['after'][4:6],
            ve['after'][7]], clock=ve['clock'], speed=ve['speed'], requestedHeading=ve['heading'],
            before=ve['before'], after=ve['after'], requested=ve['requested'],
            fineFlagsBefore=ve['fineFlagsBefore'], fineFlagsAfter=ve['fineFlagsAfter']))
    need(set(roles.values()) == set(range(7)), 'complete actor map')
    return dict(scope=SCOPE, clock=CLOCK, roles=ROLES, rows=result)


def projection_bytes(projection):
    return (json.dumps(projection, separators=(',', ':')) + '\n').encode()


def render(projection, probe):
    out = ['/* Repeated B/C diagnostic, entire declared tick0..240. No same-map unhooked control.\n',
           ' * Arrival clock is paired velocity clock, not an independently captured arrival timestamp.\n',
           f' * Full external projection SHA256 {sha(projection_bytes(projection))}. */\n',
           '#ifndef RETAIL_PATROL_LIFETIME_POLLS_H\n#define RETAIL_PATROL_LIFETIME_POLLS_H\n',
           'typedef struct { unsigned phase,role; uint32_t words[10]; } patrolLifetimeNativeRow_t;\n',
           'static patrolLifetimeNativeRow_t const patrol_lifetime_native[] = {\n']
    for row in projection['rows']:
        words = ','.join(f'0x{x:08x}u' for x in row['words'])
        out.append(f"    {{{row['phase']},{row['role']},{{{words}}}}},\n")
    out.append('};\nstatic char const patrol_lifetime_probe[] =\n')
    lines = probe.decode().splitlines(keepends=True)
    for i, line in enumerate(lines):
        out.append('    ' + json.dumps(line) + (';\n' if i == len(lines)-1 else '\n'))
    out.append('#endif\n')
    return ''.join(out).encode()


def verify(capture_dir=FIXTURES, header=HEADER):
    spec = json.loads(PROVENANCE.read_text())
    probe = PROBE.read_bytes()
    need(sha(probe) == spec['probe_sha256'], 'literal probe bytes')
    streams, public_streams, source_pins = [], [], []
    for name, expected in spec['captures'].items():
        raw = (capture_dir / name).read_bytes()
        need(sha(raw) == expected['sha256'], 'compressed capture bytes')
        decoded = gzip.decompress(raw)
        need(sha(decoded) == expected['uncompressed_sha256'], 'uncompressed capture bytes')
        records = [json.loads(line) for line in decoded.splitlines()]
        need(records[0]['sha256'] == spec['native_binary_sha256'], 'native binary')
        need(records[0]['source_sha256']['wc3_patrol_lifetime_probe.j'] == sha(probe), 'capture probe pin')
        need(records[0]['source_sha256']['map'] == spec['map_sha256'], 'capture map pin')
        streams.append(project(records))
        first = next(i for i, row in enumerate(records) if row.get('value') == START)
        last = next(i for i, row in enumerate(records) if row.get('value') == END)
        public_streams.append([row['value'] for row in records[first:last+1]
                               if isinstance(row.get('value'), str) and row['value'].startswith(('PATHMETA ', 'PATHTRACE '))])
        source_pins.append(records[0]['source_sha256'])
    need(len(streams) == 2 and streams[0] == streams[1], 'complete B/C numerical repeat')
    need(public_streams[0] == public_streams[1], 'declared public stream repeat')
    need(source_pins[0] == source_pins[1], 'capture source identity')
    projection = streams[0]
    need(sha(projection_bytes(projection)) == spec['projection_sha256'], 'complete projection pin')
    canonical = render(projection, probe)
    need(header.read_bytes() == canonical, 'literal header rows/probe differ from native canonical rendering')
    return projection, canonical


def mutation_checks(projection, canonical):
    tests = []
    for name, mutate in [
        ('missing', lambda rows: rows.pop()),
        ('extra', lambda rows: rows.append(copy.deepcopy(rows[-1]))),
        ('reorder', lambda rows: rows.__setitem__(slice(0, 2), rows[0:2][::-1])),
        ('role', lambda rows: rows[0].__setitem__('role', 0)),
        ('phase', lambda rows: rows[0].__setitem__('phase', 2)),
        ('clock', lambda rows: rows[0]['words'].__setitem__(0, rows[0]['words'][0] ^ 1)),
        ('numeric', lambda rows: rows[1]['words'].__setitem__(3, rows[1]['words'][3] ^ 1))]:
        altered = copy.deepcopy(projection)
        mutate(altered['rows'])
        need(render(altered, PROBE.read_bytes()) != canonical, 'mutation escaped: ' + name)
        tests.append(name)
    need(render(projection, PROBE.read_bytes() + b'\n') != canonical, 'probe mutation escaped')
    return tests + ['probe']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-dir', type=Path, default=FIXTURES)
    parser.add_argument('--header', type=Path, default=HEADER)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--mutation-checks', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    need(not (args.check and args.output), '--check cannot write output')
    projection, canonical = verify(args.capture_dir, args.header)
    mutations = mutation_checks(projection, canonical) if args.mutation_checks else []
    if args.output:
        with args.output.open('xb') as stream:
            stream.write(canonical)
    print(json.dumps(dict(passed=True, rows=len(projection['rows']), mutations=mutations,
                         historical_unhooked_control=False, engine_parity=False)))

if __name__ == '__main__':
    main()
