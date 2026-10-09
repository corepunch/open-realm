#!/usr/bin/env python3
"""Replay unchanged retail bodies and check complete crowd/control consumer fixtures."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile

HERE = Path(__file__).resolve().parent
FIXTURES = HERE / 'fixtures'
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE.parent / 'frida/research'))
from sep_research_analyze import Capture, preload_rows
from sep_research_expected import norm
from research.sep_research_oracle import Oracle
from research.export_separation_crowds import render, OUTPUT

spec = importlib.util.spec_from_file_location('crowd_replay', HERE / 'research/verify_SEP-02.2_replay.py')
replay_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay_module)


def frozen(name, expected):
    raw = gzip.decompress((FIXTURES / name).read_bytes())
    if hashlib.sha256(raw).hexdigest() != expected:
        raise ValueError('changed frozen contract: ' + name)
    return json.loads(raw)


def check(binary):
    bundle = frozen('retail-separation-crowd-inputs-1.27.json.gz',
                    'ff4f2d7e0ff176499544af1a2807b4818d28ea5983b13eb0e9af33b19e0851cb')
    fixtures = frozen('retail-separation-crowds-1.27.json.gz',
                      'f1d90409d57cd3e6b965eebda034d42dbcfc329284854e1d2b3ac8713805f650')
    if OUTPUT.read_text() != render():
        raise ValueError('engine literal fixture differs')
    oracle = Oracle(binary)
    result = {}
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        if set(bundle['files']) != set(bundle['sha256']):
            raise ValueError('incomplete capture hash inventory')
        for name, text in bundle['files'].items():
            relative = Path(name)
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('unsafe capture path')
            if hashlib.sha256(text.encode()).hexdigest() != bundle['sha256'][name]:
                raise ValueError('capture changed: ' + name)
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        for fixture in fixtures:
            variant = fixture['variant']
            if fixture['binary_sha256'] != oracle.sha:
                raise ValueError('unsupported original binary')
            captures = [Capture(root / variant / f'observe-{i}', root / variant / 'map.json') for i in (1, 2)]
            expected = frozen(f'research/SEP-04.{2 if variant == "crowd" else 3}-expected.json.gz',
                              '997e6ee7e15b1f9187f2c4313ceaddb5c02cfb0a3a1a1d03f58a1340d0d403bf' if variant == 'crowd' else
                              'a0e0751360e39fd89d67c19e2fc1384cef45d813171cd25b322fe0bdd6d48807')
            groups = [expected['sequences']] if variant == 'crowd' else expected['separationSequences']
            normalized = []
            for cap in captures:
                if not cap.complete or any(r.get('event') in ('script-error', 'trace-failed', 'error') for r in cap.rows):
                    raise ValueError('incomplete captured run')
                rows, stats = replay_module.replay(cap, oracle)
                if not rows or not all(r['match'] for r in rows):
                    raise ValueError('original body replay mismatch')
                actual = [[norm(r) for r in rows if r['i'] in group['units']] for group in groups]
                if actual != [g['sequence'] for g in groups]:
                    raise ValueError('complete frozen sequence differs')
                normalized.append(actual)
            if normalized[0] != normalized[1]:
                raise ValueError('repeated original owner sequence differs')
            control = preload_rows((root / variant / 'control/preload.txt').read_text())
            if not control or any(cap.preload != control for cap in captures):
                raise ValueError('observer-free public control differs')
            cap = captures[1]
            for observed, wanted in zip(cap.updates, fixture['visits']):
                if observed['ownerBefore'] != wanted['random_before'] or observed['ownerAfter'] != wanted['random_after']:
                    raise ValueError('full owner RNG boundary differs')
                if observed['tick'] != wanted['tick'] or observed['visit'] != wanted['visit']:
                    raise ValueError('full owner schedule differs')
            if len(cap.updates) != len(fixture['visits']):
                raise ValueError('missing complete engine visit')
            inverse = {m: i for i, m in cap.mover_of.items()}
            retries = [dict(unit=inverse[r['current']],tick=r['tick'],visit=r['visit'],before=r['before'],after=r['after'],
                            result=r['result'],random_before=r['ownerBefore'],random_after=r['ownerAfter'])
                       for r in cap.rows if r['event'] == 'retry-result']
            if retries != fixture['retries']:
                raise ValueError('full attributed retry stream differs')
            result[variant] = dict(visits=len(cap.updates),pairs=stats['pairs'],bodies=stats['body'],
                                   retries=len(retries),public_markers=len(control),observed_repeats=2,observer_free_controls=1)
        live = frozen('retail-crowd-owner-live-1.27.json.gz',
                      'e1ef834fc463f885aa8ede23304d78727a27f97614da791da9cf07f91d6ed374')
        meta = root / 'owner-map.json'
        meta.write_text(json.dumps(live['map']))
        for name, data in live['files'].items():
            relative = Path(name)
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('unsafe live owner path')
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data)
        for name in ('ground-inputs-first', 'ground-inputs-repeat'):
            cap = Capture(root / name, meta)
            if not cap.complete or cap.preload != control:
                raise ValueError('live physical-owner public control differs')
            inverse = {m: i for i, m in cap.mover_of.items()}
            normalized = []
            def owner(value):
                return dict(flags=value['flags'], raw48=value['raw48'], members=[
                    dict(unit=inverse.get(m['mover']), row=m['row'][:5]+m['row'][6:],
                         pose=m['pose'], requested=m['requested']) for m in value['members']])
            for row in cap.rows:
                if row['event'] == 'crowd-retry-input':
                    normalized.append({k: row[k] for k in ('counter', 'source', 'adjusted', 'coarseCount', 'coarseIndex', 'coarse')} |
                                      dict(unit=inverse[row['mover']]))
                elif row['event'] == 'crowd-physical-owner':
                    normalized.append(dict(counter=row['counter'], before=owner(row['before']), after=owner(row['after'])))
            if normalized != live['normalized']:
                raise ValueError('live physical-owner or adjusted retry stream differs')
        saved = json.loads((FIXTURES / 'retail-crowds-ghidra-1.27.json').read_text())
        if saved['unsaved'] or saved['binary_sha256'] != oracle.sha or len(saved['rows']) != 7:
            raise ValueError('missing saved Ghidra evidence')
        if any('Payoff144' not in row['comment'] for row in saved['rows']):
            raise ValueError('incomplete saved crowd evidence')
        result['physical_owners'] = dict(observed_repeats=2, rows=len(live['normalized']),
                                        public_markers=len(control), saved_functions=len(saved['rows']))
    return dict(binary_sha256=oracle.sha,checks=result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = check(args.binary)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
