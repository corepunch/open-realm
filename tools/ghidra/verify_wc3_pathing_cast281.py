#!/usr/bin/env python3
"""Verify original point CHANNEL ownership; preserve the later cast-phase gaps."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from verify_wc3_pathing_target_normalize218 import original_bytes

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'tools/ghidra/fixtures/retail-cast281-1.27.json'
BUNDLE = SPEC.with_suffix('.json.gz')
SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
TESTS = ['wc3_spell.cast281*', 'wc3_interrupt.actual_spell_completion_replaces_owner_and_releases_removed_peers_once',
         'wc3_movement.flight280*', 'wc3_movement.structure279*', 'wc3_movement.spell269*',
         'wc3_movement.spell185*', 'wc3_spell.owner268*', 'wc3_spell.owner267*',
         'wc3_spell.point269*', 'wc3_save.current_format_uses_current_entity_layout']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def markers(text):
    return re.findall(r'call Preload\( "(C281 [^"\r\n]*)" \)', text)


def normalized(public):
    result = []
    for text in public:
        if 'context' in text:
            handles = re.findall(r' (?:caster|trigger)=(\d+)', text)
            if len(handles) != 2 or handles[0] != handles[1] or int(handles[0]) == 0:
                raise ValueError('raw caster/trigger identity')
            text = re.sub(r' (caster|trigger)=\d+', r' \1=CASTER', text)
        result.append(text)
    return result


def decisions(rows):
    # Retain original clock/counter/flags and family order. Process pointers are
    # asserted relationally below, never compared as simulation words.
    out = []
    phase = None
    scene = -1
    for row in rows:
        event = row['event']
        if event == 'marker':
            scene = int(re.search(r'scene=(-?\d+)', row['value'])[1])
        if event in ('cast-begin', 'cast-effect', 'channel-producer'):
            if event == 'cast-begin' and row['phase'] == 'begin':
                phase = row
            if phase is None or row.get('ability', phase['ability']) != phase['ability']:
                raise ValueError('native cast ownership')
            if 'caster' in row and row['caster'] != phase['caster']:
                raise ValueError('native caster ownership')
            out.append([scene, event, row['phase'], row['c'], row['clock'], row.get('flags')])
        elif event == 'marker' and any(' label='+label+' ' in row['value'] for label in
                ('channel-before', 'channel-after', 'unit-channel', 'effect')):
            out.append([scene, 'public', row['c'], normalized([row['value']])[0]])
    counts = Counter(row[1] for row in out)
    if counts != {'cast-begin': 16, 'cast-effect': 4, 'channel-producer': 16, 'public': 26}:
        raise ValueError('native channel/cast counts')
    return out


def validate_runtime(bundle, spec):
    captures = bundle['captures']
    if len(captures) != 3 or [c['rows'][0]['mode'] for c in captures] != ['observe', 'observe', 'control']:
        raise ValueError('repeats/control required')
    repeated = []
    for cap in captures:
        rows = cap['rows']
        meta, footer = rows[0], rows[-1]
        public = markers(cap['preload'])
        if (meta['task'] != 'POINT-CHANNEL' or meta['sha256'] != SHA or not meta['owned']
                or meta['env'] not in ('B', 'C') or meta['source_sha256']['map'] != spec['map_sha256']
                or not footer.get('complete') or footer['markers'] != len(public)
                or hashlib.sha256(cap['preload'].encode()).hexdigest() != footer['sha256']
                or any(r.get('type') == 'error' or r['event'] == 'trace-failed' for r in rows)):
            raise ValueError('capture provenance/completion')
        for name in ('cast281_capture.py', 'cast281_observer.js', 'cast281_make_map.py', 'cast281_probe.j'):
            if meta['source_sha256'][name] != spec['source_sha256']['tools/frida/research/' + name]:
                raise ValueError('captured source differs')
        if normalized(public) != spec['public'] or len(public) != 715:
            raise ValueError('original public channel differs')
        if meta['mode'] == 'control':
            if any(r['event'] in ('trace-end', 'gtick', 'cast-begin', 'channel-producer') for r in rows):
                raise ValueError('hooked control')
        else:
            live = [r['value'] for r in rows if r['event'] == 'marker' and ' label=start' not in r['value']]
            if live != public:
                raise ValueError('live public markers differ')
            ends = [r for r in rows if r['event'] == 'trace-end']
            if len(ends) != 1 or not ends[0]['installed'] or any(k.endswith('-dropped') and v for k, v in ends[0]['counts'].items()):
                raise ValueError('observer incomplete')
            counts = Counter(r['event'] for r in rows)
            for event in ('marker', 'cast-begin', 'cast-effect', 'channel-producer', 'gtick', 'gtick-end'):
                if counts[event] != ends[0]['counts'][event]:
                    raise ValueError('observer incomplete')
            repeated.append(decisions(rows))
    if repeated[0] != repeated[1] or repeated[0] != spec['contract']:
        raise ValueError('repeated channel/cast words differ')
    return dict(captures=2, controls=1, public_markers=715, channel_cases=8, cancellation_cases=6)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, required=True)
    ap.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    ap.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    ap.add_argument('--report', type=Path, required=True)
    a = ap.parse_args()
    if a.report.exists():
        ap.error('fresh report required')
    spec = json.loads(SPEC.read_text())
    if spec['game_sha256'] != SHA or spec['engine_patterns'] != TESTS or len(spec['instructions']) != 1162:
        raise ValueError('contract changed')
    for name, expected in spec['source_sha256'].items():
        if digest(ROOT / name) != expected:
            raise ValueError('source pin ' + name)
    if digest(BUNDLE) != spec['bundle_sha256']:
        raise ValueError('damaged frozen captures')
    evidence = validate_runtime(json.loads(gzip.decompress(BUNDLE.read_bytes())), spec)
    original_bytes(a.binary, spec['instructions'])
    engine = []
    for tft in (False, True):
        for i, pattern in enumerate(TESTS):
            stem = a.report.with_name(a.report.stem + ('-tft' if tft else '-classic') + f'-{i}')
            xml, log = stem.with_suffix('.xml'), stem.with_suffix('.log')
            if xml.exists() or log.exists():
                raise ValueError('fresh engine outputs required')
            with log.open('w') as out:
                r = subprocess.run([str(a.test_binary.resolve()), '-data', str(a.data.resolve()),
                                    *(['-tft'] if tft else []), '+dedicated', '1', '+test', pattern],
                                   stdout=out, stderr=subprocess.STDOUT, timeout=120,
                                   env={**os.environ, 'TEST_JUNIT': str(xml.resolve())})
            x = ET.parse(xml).getroot()
            cases = list(x.iter('testcase'))
            if r.returncode or not cases or any(c.findall('failure') for c in cases):
                raise ValueError('engine ' + str(log))
            engine.append(dict(tft=tft, pattern=pattern, tests=len(cases), assertions=int(x.get('assertions'))))
    report = dict(status='verified', passed=True, binary_sha256=SHA, instructions=len(spec['instructions']),
                  **evidence, engine=engine, scope=spec['scope'])
    a.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
