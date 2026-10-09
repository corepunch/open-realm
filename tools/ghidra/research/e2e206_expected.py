#!/usr/bin/env python3
"""Freeze combined bindings; existing retail outputs are never regenerated here."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from research.e2e206_contract import ROOT, CATEGORIES, SOURCES, fingerprint, validate


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
        'dynamic': ('ROUTE-03 dynamic insertion/removal, moving peers, terrain closure/reopening; '
                    'ROUTE-05 asymmetric yielding. Exact poses/velocities/facing, fine/adaptive buffers, '
                    'waits/retries, blocker roles, timestamps and charged work at every observed owner step.',
                    'All thirteen engine journeys retain captured order retirement; same/different-player '
                    'tunnel orders end through cannot-path, rather than arrival. Each cold save matches '
                    'the uninterrupted retail suffix.'),
        'pursuit': ('TARGET-02 walled ground Smart approach:235 raw owner rows and85 saved suffix rows; '
                    'TARGET-03 fogged cached arrival:238 raw25-word rows and28 saved suffix rows. '
                    'Supplemental engine tests retain denied-owner stop/refresh/queue recovery and '
                    'discarded changed-target samples through the17-visit cadence.',
                    'The initial Smart approach capture ends while the order is active; its final numerical '
                    'state is checked without claiming arrival. Hidden approach/persistent-follow cases '
                    'end at the cached arrival and retire the public order.'),
    }
    spec = dict(version=1, task='E2E-01.2', parent=SOURCES[0], build=base['build'],
                pins={path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in SOURCES},
                engine_repeats=2, engine_editions=['classic', 'tft'], categories=[])
    for name, (test, ids) in CATEGORIES.items():
        spec['categories'].append(dict(id=name, test=test, entries=ids,
                                       entry_sha256={identity: fingerprint(entries[identity]) for identity in ids},
                                       scope=scopes[name][0], completion=scopes[name][1]))
    spec['exclusions'] = [
        'No new live run or gameplay-policy claim. Complete archived Frida contracts are freshly revalidated unchanged.',
        'The parent static/disconnected source and route pins are validated; its full original runner is a separate entry.',
        'Some dynamic scenes have only one observation or no scene-specific observer-free control; child exclusions remain binding.',
        'Final facing without a following observation and unpublished empty-buffer destinations remain unasserted.',
        'Pursuit crowd evidence proves native denial/stop/layout ordering, not full crowd numerical engine parity. '
        'Unwritten failure-result stack words are excluded.',
        'Ground Smart approach does not cover all TARGET-02 ability/movement families. Hidden arrival does not close '
        'all TARGET-03 visibility/loss producers.',
        'Saved engine continuations compare with uninterrupted retail captures, not retail saves. '
        'No whole-world, full-RNG, all-scenario determinism or frame-budget claim.',
    ]
    validate(spec, manifest)
    a.output.write_text(json.dumps(spec, indent=2) + '\n')
    print(json.dumps(dict(categories=len(CATEGORIES), original_contracts=3, literal_source_pins=len(SOURCES))))


if __name__ == '__main__':
    main()
