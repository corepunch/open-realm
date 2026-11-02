#!/usr/bin/env python3
"""Freeze combined baseline bindings without regenerating existing expectations."""
import argparse
import hashlib
import json
from pathlib import Path
from e2e205_contract import ROOT, CATEGORIES, SOURCES, fingerprint, validate


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        p.error('combined fixture must be new')
    manifest = json.loads((ROOT / 'tools/ghidra/fixtures/retail-pathfinding-corpus-1.27.json').read_text())
    base = json.loads((ROOT / SOURCES[0]).read_text())
    entries = {row['id']: row for row in manifest['entries']}
    scopes = {
        'formation': ('FORM-05 selected mixed-rank order, independent singleton controls, narrow passage, '
                      'classification, caps, offsets, route/regroup state and saved complete suffixes.',
                      'Each captured selection/independent journey ends with order zero and its frozen final public positions.'),
        'crowd': ('SEP-04 mixed owner/radius/rank/selector wall crowd plus enabled/disabled ground controls; '
                  'ordered contributions, PRNG, endpoints, occupancy, retries and saved mid-order continuation.',
                  'Complete original owner/public stream through scripted Stop/removal; four blocked mixed-crowd orders '
                  'remain unfinished at the final pre-removal sample. Completion is not inferred from the fixture ending.'),
        'gate': ('GATE group crossing, cached disabled-edge skip, sole crossing disconnection, '
                 'chained activation combinations, complete motion/retry/consumer order and saved suffixes.',
                 'Open skip walks and regroups; disconnected case retries then force-completes; '
                 'chains preserve route/consumer ordering for all four activation combinations.'),
    }
    spec = dict(version=1, task='E2E-01.4', parent=SOURCES[0], build=base['build'],
                pins={path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in SOURCES},
                engine_repeats=2, engine_editions=['classic', 'tft'], categories=[])
    for name, (test, ids) in CATEGORIES.items():
        spec['categories'].append(dict(id=name, test=test, entries=ids,
                                       entry_sha256={identity: fingerprint(entries[identity]) for identity in ids},
                                       scope=scopes[name][0], completion=scopes[name][1]))
    spec['exclusions'] = [
        'No new live or gameplay-policy claim; complete original child contracts are freshly revalidated unchanged.',
        'The parent static/disconnected source pins and routes are validated; its complete original runner remains a separate corpus entry.',
        'Formation tests supply original owner clocks and WPM geometry. Final inferred clock and pre-commit versus post-commit '
        'limitations remain exactly those of FORM-05 child contracts.',
        'Selection no-hook click timing controls retain their original bounded exclusions; combined acceptance cannot strengthen them.',
        'Crowd completion does not imply every blocked order arrived. First blocking identity remains inferred where recorded.',
        'Engine saved continuations are not retail save/load proof. No whole-world, full-RNG, all-scenario determinism or frame-budget claim.']
    validate(spec, manifest)
    a.output.write_text(json.dumps(spec, indent=2) + '\n')
    print(json.dumps(dict(categories=3, original_contracts=6, literal_source_pins=len(SOURCES))))


if __name__ == '__main__':
    main()
