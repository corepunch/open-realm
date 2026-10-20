#!/usr/bin/env python3
"""Check complete read-only removal repeats, control and actual game regression."""
import argparse
import copy
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'tools/ghidra/fixtures/retail-removal-separation204-1.27.json.gz'
HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TEST = 'wc3_repulsion_policy.removal_retires_membership_before_callbacks_and_survives_save'
EVENTS = ('marker', 'refresh-begin', 'refresh-end', 'configure-begin', 'configure-end',
          'remove-begin', 'remove-end', 'acquire-begin', 'acquire-end', 'release-begin',
          'release-end', 'inactive-begin', 'inactive-end', 'owner-begin', 'owner-end', 'destroy')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def markers(text):
    return re.findall(r'call Preload\( "(R204 [^"\r\n]*)" \)', text)


def stages(rows):
    result = copy.deepcopy([r for r in rows if r['event'] in EVENTS])
    for row in result:
        for unit in [row.get('unit')] + row.get('units', []):
            if unit is not None:
                del unit['pointer']
        # Retain all raw stack words in the capture. Only AL/AX/AL reach the
        # setters; the other bytes are uninitialized native caller storage.
        if 'args' in row:
            row['args'][1:] = [row['args'][1] & 255, row['args'][2] & 65535, row['args'][3] & 255]
    return result


def claims(sequence):
    begin = [r['unit'] for r in sequence if r['event'] == 'remove-begin']
    end = [r['unit'] for r in sequence if r['event'] == 'remove-end']
    if len(begin) != 3 or len(end) != 3 or [u['depth'] for u in begin] != [0, 0, 1]:
        raise ValueError('removal producer inventory differs')
    if any(u['code'] != int.from_bytes(b'hREM', 'big') for u in begin):
        raise ValueError('wrong authored unit')
    if any(u['sep'] is None or u['sep'] & 0xffff0000 != 0x10110000 for u in begin[:2]) or begin[2]['sep'] is not None:
        raise ValueError('enabled and paused removal controls differ')
    if any(u['depth'] != 1 or u['sep'] is not None for u in end):
        raise ValueError('removal did not acquire suppression before return')
    labels = ('nested_removed', 'nested_owner', 'nested_paused', 'nested_resumed')
    nested = [r for r in sequence if r['event'] == 'marker' and any(' label=' + s + ' ' in r['value'] for s in labels)]
    if len(nested) != 4 or any(len(r['units']) != 1 for r in nested):
        raise ValueError('nested refresh boundary omitted')
    units = [r['units'][0] for r in nested]
    if (any(u['depth'] != 1 or u['sep'] is not None for u in units) or
        [u['owner'] for u in units] != [0, 1, 1, 1] or [u['f54'] for u in units] != [0, 0, 1, 0]):
        raise ValueError('owner/pause refresh bypassed live removal suppression')
    if any(u['identity'] != begin[0]['identity'] for u in units):
        raise ValueError('pending identity changed')
    acquired = [r for r in sequence if r['event'] == 'acquire-end']
    if len(acquired) != 4 or any(r['unit']['sep'] is not None for r in acquired):
        raise ValueError('suppression left live separation')
    destroyed = [r['unit'] for r in sequence if r['event'] == 'destroy']
    # Retail releases its count and briefly configures again before destruction.
    # Preserve that witness; the engine integration covers pending removal only.
    if len(destroyed) != 3 or any(u['depth'] != 0 or u['sep'] is None for u in destroyed):
        raise ValueError('retirement witness changed or was omitted')


def validate(spec, archive=None):
    if spec['version'] != 1 or spec['task'] != 'SEP-01.2/removal' or spec['binary_sha256'] != HASH:
        raise ValueError('unsupported removal contract')
    if spec['map']['units'] != {'hREM': {'urpo': 2, 'urpp': 17, 'urpg': 17, 'urpr': 17}}:
        raise ValueError('authored non-stock policy differs')
    observed, public = [], []
    for capture in spec['captures']:
        raw = ''.join(json.dumps(r) + '\n' for r in capture['rows']).encode()
        if digest(raw) != capture['sha256'] or len(raw) != capture['bytes']:
            raise ValueError('complete capture pin differs')
        if archive is not None and (archive / capture['name']).read_bytes() != raw:
            raise ValueError('archived capture differs')
        rows = capture['rows']
        meta = rows[0]
        if (meta['event'] != 'metadata' or meta['sha256'] != HASH or not meta['owned'] or
            meta['task'] != 'SEP-01.2/payoff204' or meta['source_sha256']['map'] != spec['map']['sha256']):
            raise ValueError('capture provenance differs')
        text = capture['preload']
        if digest(text.encode()) != capture['preload_sha256']:
            raise ValueError('public control pin differs')
        if archive is not None and (archive / (Path(capture['name']).stem + '-preload.txt')).read_bytes() != text.encode():
            raise ValueError('archived public control differs')
        footer = [r for r in rows if r['event'] == 'preload-file']
        stream = markers(text)
        if (len(footer) != 1 or footer[0].get('complete') is not True or footer[0]['markers'] != 44 or
            footer[0]['sha256'] != capture['preload_sha256'] or len(stream) != 44 or ' label=complete ' not in stream[-1]):
            raise ValueError('public continuation incomplete')
        public.append(stream)
        if meta['mode'] == 'observe':
            allowed = set(EVENTS) | {'metadata', 'installed', 'loading-key', 'trace-end', 'preload-file'}
            finish = [r for r in rows if r['event'] == 'trace-end']
            if (any(r['event'] not in allowed for r in rows) or len(finish) != 1 or
                finish[0]['installed'] is not True or [r['value'] for r in rows if r['event'] == 'marker'] != stream):
                raise ValueError('observer failed or changed public continuation')
            counts = {e: sum(r['event'] == e for r in rows) for e in (*EVENTS, 'installed')}
            if finish[0]['counts'] != counts:
                raise ValueError('observer event accounting differs')
            sequence = stages(rows)
            claims(sequence)
            observed.append(sequence)
        elif meta['mode'] != 'control' or any(r['event'] not in
                ('metadata', 'loading-key', 'control-start-file', 'preload-file') for r in rows):
            raise ValueError('control contains instrumentation')
    if len(observed) != 2 or observed[0] != observed[1] or observed[0] != spec['stages']:
        raise ValueError('complete repeated native stages differ')
    if len(public) != 3 or any(p != public[0] for p in public) or public[0] != spec['public_markers']:
        raise ValueError('observer-free continuation differs')
    if archive is not None and digest((archive / spec['map']['name']).read_bytes()) != spec['map']['sha256']:
        raise ValueError('archived map differs')
    return dict(passed=True, status='removal-separation-boundary', observations=2, controls=1,
                removals=6, nested_refresh_boundaries=8, public_markers=44, native_stages=286,
                binary_sha256=HASH)


def engine(binary, data, work):
    results = {}
    for edition in ('classic', 'tft'):
        junit, log = work / (edition + '.xml'), work / (edition + '.log')
        cmd = [str(binary.resolve()), '-data', str(data.resolve())]
        if edition == 'tft':
            cmd += ['-tft']
        cmd += ['+dedicated', '1', '+test', TEST]
        with log.open('w') as out:
            child = subprocess.run(cmd, cwd=ROOT, env=dict(os.environ, TEST_JUNIT=str(junit)),
                                   stdout=out, stderr=subprocess.STDOUT, timeout=180)
        if child.returncode:
            raise ValueError('engine removal regression failed: ' + edition)
        suite = ET.parse(junit).getroot()
        cases = suite.findall('testcase')
        if (suite.attrib['tests'] != '1' or suite.attrib['failures'] != '0' or suite.attrib['errors'] != '0' or
            suite.attrib['skipped'] != '0' or int(suite.attrib['assertions']) != 117 or
            len(cases) != 1 or cases[0].attrib['name'] != TEST or list(cases[0])):
            raise ValueError('missing/failed/empty engine removal regression')
        results[edition] = dict(assertions=117, junit_sha256=digest(junit.read_bytes()), log_sha256=digest(log.read_bytes()))
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--archive', type=Path, required=True, help='report root; uses research/SEP-01.2/integration204')
    p.add_argument('--fixture', type=Path, default=FIXTURE)
    p.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    p.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    if a.report.exists():
        p.error('report must be fresh')
    if digest(a.binary.read_bytes()) != HASH:
        p.error('unsupported retail binary')
    spec = json.loads(gzip.decompress(a.fixture.read_bytes()))
    result = validate(spec, a.archive / 'research/SEP-01.2/integration204')
    sys.path.insert(0, str(ROOT / 'tools/ghidra/research'))
    from verify_separation_eligibility import verify
    predicate = verify(a.binary)
    if predicate != json.loads((ROOT / 'tools/ghidra/fixtures/retail-separation-eligibility-1.27.json').read_text()):
        raise ValueError('original predicate differs from unchanged retail expectations')
    result['original_predicate_cases'] = len(predicate['cases'])
    work = a.report.with_suffix('.work')
    work.mkdir(parents=True, exist_ok=False)
    result['engine'] = engine(a.test_binary, a.data, work)
    result['exclusions'] = spec['exclusions']
    a.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
