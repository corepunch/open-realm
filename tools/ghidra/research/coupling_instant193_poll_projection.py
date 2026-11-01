#!/usr/bin/env python3
"""Verify/reproduce the native193 literal poll fixture without rewriting evidence.

Example:
  python3 tools/ghidra/research/coupling_instant193_poll_projection.py \
    --capture-dir /path/to/range333-v1 --check

The pinned portable range corpus auditor verifies fresh capture/control bytes read-only.
Phase1 is intentionally excluded: no corresponding native record was captured.
The sibling .h initializer must exactly match all 532 projected rows.
Optional --engine-log checks every phase0/phase2 raw word in global order.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'games/warcraft-3/game/tests/fixtures/retail_couple_instant193_polls.json'
ROLES = ['Archer', 'Hippo']
INDEX = {0: [2, 3, 4, 5, 6, 7, 8, 18, 10, 11], 2: [2, 13, 14, 15, 16, 7]}
ARCHER_X = [1102053376]*4 + [1109393408]*2 + [1108434944,1108432896,1108436992]


def need(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def project(path):
    identities, result = {}, []
    for line in path.read_text().splitlines():
        row = json.loads(line)
        if row.get('event') not in ('arrival', 'velocity'):
            continue
        key = (row['scene'], row['before']['mover'])
        if key not in identities:
            scene = row['scene']
            need(type(scene) is int and 0 <= scene < 9, 'Scene identity')
            speed = row['before']['pose'][6]
            need(speed in (0x41070000, 0x41480000), 'Authored actor speed')
            role = 0 if speed == 0x41070000 else 1
            xy = tuple(row['before']['pose'][2:4])
            need(xy == (ARCHER_X[scene] if role == 0 else 1111490560, 1107296256),
                 'Initial actor pose/role')
            need((row['scene'], role) not in identities.values(), 'Ambiguous actor role')
            identities[key] = (row['scene'], role)
        if row['event'] == 'arrival':
            phase = 0
            words = [row['clock']['primary'][0], *row['source'], *row['destination'],
                     row['heading'], row['threshold'], row['angle'], row['inside'], row['result']]
        else:
            phase = 2
            words = [row['clock']['primary'][0], *row['after']['pose'][2:6], row['after']['heading']]
        need(len(words) == len(INDEX[phase]), 'Native schema width')
        need(all(type(w) is int and 0 <= w <= 0xffffffff for w in words), 'Raw uint32 words')
        result.append(dict(scene=row['scene'], phase=phase, role=identities[key][1], words=words))
    return result


def engine_projection(path):
    result = []
    for line in path.read_text().splitlines():
        if 'ACOIPOLL scene=' not in line:
            continue
        match = re.search(r'ACOIPOLL scene=(\d+) ((?:\d+\s*)+)$', line)
        need(match is not None, 'Malformed engine trace row')
        words = list(map(int, match[2].split()))
        need(len(words) == 20 and words[0] in (0, 1, 2) and 0 <= words[1] < 2,
             'Engine trace schema')
        if words[0] != 1:
            result.append(dict(scene=int(match[1]), phase=words[0], role=words[1],
                               words=[words[i] for i in INDEX[words[0]]]))
    return result



def verify_header(path, rows):
    """Check the entire initializer, rejecting extra tokens or changed row widths."""
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(), flags=re.S)
    declarations = list(re.finditer(
        r'static\s+coupleNativePoll193_t\s+const\s+couple_native_polls193\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;',
        source, re.S))
    need(len(declarations) == 1, 'Header must have one literal poll initializer')
    actual = re.sub(r'\s+', '', declarations[0][1])
    expected = ''.join(
        '{%d,%d,%d,%d,{%s}},' %
        (row['scene'], row['phase'], row['role'], len(row['words']),
         ','.join('0x%08xu' % word for word in row['words'])) for row in rows)
    need(actual == expected, 'Header ordered scene/phase/role/count/raw words differ')


def verify(capture_dir, fixture=FIXTURE, engine_log=None):
    provenance = json.loads(fixture.with_suffix('.provenance.json').read_text())
    need(digest(fixture) == provenance['projection_sha256'], 'Frozen projection SHA256')
    expected = json.loads(fixture.read_text())
    need(expected['roles'] == ROLES and expected['engine_word_indices'] ==
         {str(k): v for k, v in INDEX.items()}, 'Projection schema')
    for name, sha in provenance['capture_files_sha256'].items():
        need(digest(capture_dir / name) == sha, 'Frozen capture/source SHA256: ' + name)
    # Reuse the portable, fully pinned range corpus auditor, avoiding the original
    # archived auditor's machine-specific mpqtool executable path.
    import gzip
    import coupling_instant_range_evidence as evidence
    evidence.audit()
    files = evidence.unpack(json.loads(gzip.decompress(evidence.BUNDLE.read_bytes())))
    for name in files:
        if (capture_dir / name).is_file():
            need((capture_dir / name).read_bytes() == files[name], 'Portable corpus bytes: ' + name)
    for name in provenance['capture_files_sha256']:
        files[name] = (capture_dir / name).read_bytes()
    summary = evidence.audit_semantics(files)
    evidence.audit_boundary(files)
    evidence.audit_schema(files)
    evidence.audit_map(files)
    need(summary == json.loads((capture_dir / 'validated-summary.json').read_text()),
         'Fresh source/public/control audit differs from frozen summary')
    need(summary['accepted_runs'] == ['observe1', 'observe3', 'control1'] and
         summary['excluded_runs'] == provenance['excluded_runs'], 'Accepted/excluded runs')
    need(summary['markers_per_capture'] == provenance['public_markers_per_capture'] and
         summary['public_stream_sha256'] == provenance['public_sha256'] == expected['public_sha256'],
         'Public stream witness')
    first = project(capture_dir / 'observe1.jsonl')
    need(len(first) == provenance['ordered_rows'] == 532, 'Exact global poll count')
    need(first == project(capture_dir / 'observe3.jsonl'), 'Repeated global poll order/words')
    need({r['scene'] for r in first} == set(range(9)), 'All nine scenes')
    need(first == expected['rows'], 'Literal fixture differs from native projection')
    verify_header(fixture.with_suffix('.h'), first)
    if engine_log is not None:
        need(engine_projection(engine_log) == first, 'Engine global poll order/raw words differ')
    return first


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-dir', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, default=FIXTURE)
    parser.add_argument('--check', action='store_true', help='Read-only verification; never write output')
    parser.add_argument('--output', type=Path, help='Optional new projection JSON; refuses overwrite')
    parser.add_argument('--engine-log', type=Path)
    args = parser.parse_args()
    if args.check and args.output:
        parser.error('--check cannot be combined with --output')
    rows = verify(args.capture_dir, args.fixture, args.engine_log)
    if args.output:
        with args.output.open('x') as stream:
            json.dump(rows, stream, indent=2)
            stream.write('\n')
    print('PASS: 532 globally ordered native polls; header, two observations and fresh unhooked control audit exact; phase1 excluded.')


if __name__ == '__main__':
    main()
