#!/usr/bin/env python3
"""Verify public Boots bonus queries separately from published Move speed caps."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re

from verify_wc3_arrival_trace import configure
from verify_wc3_motion_trace import verify as verify_motion
from verify_wc3_speed_inputs import bits


def digest(rows):
    selected = [{k: v for k, v in r.items()
                 if k not in ('ms', 'unit', 'handle', 'identity', 'mover', 'ability')}
                for r in rows if r.get('event', '').startswith('speed-')]
    return hashlib.sha256(json.dumps(selected, separators=(',', ':')).encode()).hexdigest()


def verify(rows, engine, fixture):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if any(r.get('type') == 'error' for r in rows): raise ValueError('item-speed observer error')
    if len(metadata) != 1 or metadata[0].get('sha256') != fixture['binary_sha256'] or metadata[0].get('source_sha256') != fixture['source_sha256']:
        raise ValueError('item-speed source/target differs')
    if not all(metadata[0].get(k) for k in ('owned', 'motionEvents', 'velocityEvents', 'taskEvents')):
        raise ValueError('item-speed observer options missing')
    if len(ends) != 1 or not ends[0].get('installed'): raise ValueError('item-speed completion missing')
    if digest(rows) != fixture['speed_sha256']: raise ValueError('item-speed producer sequence differs')
    for kind, count in fixture['counts'].items():
        if sum(r.get('event') == kind for r in rows) != count or ends[0].get('counts', {}).get(kind, 0) != count:
            raise ValueError('item-speed truncated ' + kind)
    markers = [re.fullmatch(r'PATHTRACE tick=(\d+) label=(\w+) x=(-?[\d.]+) y=(-?[\d.]+) order=(\d+)', r.get('value', ''))
               for r in rows if r.get('event') == 'marker']
    if any(m is None for m in markers): raise ValueError('item-speed malformed marker')
    labels = [m[2] for m in markers]
    if any(labels.count(k) != 1 for k in (fixture['start_label'], 'order_accepted', 'complete')) or any(k.endswith('_failed') or k == 'order_rejected' for k in labels):
        raise ValueError('item-speed inventory/order admission failed')
    if [int(m[1]) for m in markers if m[2] == 'sample'] != list(range(1, 301)):
        raise ValueError('item-speed samples incomplete')
    engine.pathing_speed_bonus.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
    engine.pathing_speed_cap.argtypes = [ctypes.POINTER(ctypes.c_uint32)] * 2
    current = dict(item_baseline=270, item_one=330, item_two=330, item_remove_one=330,
                   item_remove_two=270, item_publish_set=330, item_reissue=330, item_publish_restore=270)
    actors, move_abilities, movers, epochs, maximum_units = set(), set(), set(), set(), set()
    bonuses = {}
    maximum = None
    published = bits(270 / 32)
    phase = None
    cached_commits = 0
    exact_compositions = 0
    for r in rows:
        kind = r.get('event')
        if kind == 'speed-marker':
            match = re.fullmatch(r'PATHSPEED case=([a-z0-9_]+)', r['value'])
            if match:
                phase = match[1]
                maximum = None
        elif kind == 'speed-flat-bonus':
            if r['authored'] != bits(60) or r['output'] != r['authored']:
                raise ValueError('item-speed authored bonus differs')
            bonuses.setdefault(r['case'], set()).add(r['ability'])
        elif kind == 'speed-flat-maximum':
            maximum = r
            maximum_units.add(r['unit'])
            if r['output'] != bits(current[r['case']] - 270): raise ValueError('item-speed maximum differs')
        elif kind == 'speed-composition':
            if maximum is None or maximum['case'] != r['case'] or r['base'] != bits(270) or r['multiplier'] != bits(1):
                raise ValueError('item-speed composition producer missing')
            move_abilities.add(r['ability'])
            out = (ctypes.c_uint32 * 3)()
            engine.pathing_speed_bonus((ctypes.c_uint32 * 8)(r['base'], maximum['output'], 0, r['multiplier'], 0, 0, bits(150), bits(400)), out)
            if list(out) != [maximum['output'], r['output'], r['output']]: raise ValueError('item-speed C composition differs')
            exact_compositions += 1
        elif kind == 'speed-native':
            if r['rawcode'] != fixture['rawcode'] or r['bounds'] != fixture['bounds']:
                raise ValueError('item-speed actor profile differs')
            actors.add((r['unit'], r['handle']))
            expected = 270 if r['name'] == 'GetUnitDefaultMoveSpeed' else current[r['case']]
            if r['name'] == 'SetUnitMoveSpeed':
                if r['input'] != bits(270): raise ValueError('item-speed setter input differs')
            elif r['output'] != bits(expected): raise ValueError('item-speed public getter differs')
        elif kind == 'speed-cap-change':
            movers.add(r['mover'])
            if r['value'] != bits(current[r['case']] / 32): raise ValueError('item-speed requested cap differs')
            out = (ctypes.c_uint32 * 10)()
            engine.pathing_speed_cap((ctypes.c_uint32 * 13)(*r['before'], *r['clock'], r['value'], r['fineFlagsBefore']), out)
            changed = int(r['case'] == 'item_publish_restore')
            if list(out) != [*r['after'], r['fineFlagsAfter'], changed]: raise ValueError('item-speed C cap transition differs')
            published = r['value']
        elif kind == 'speed-publication':
            movers.add(r['mover']); epochs.add(tuple(r['identity']))
            if r['rawcode'] != fixture['rawcode'] or r['input'] != bits(current[r['case']]) or r['limit'] != bits(current[r['case']] / 32) or r['increment'] != r['limit']:
                raise ValueError('item-speed bridge publication differs')
        elif kind == 'velocity-commit':
            movers.add(r['mover'])
            if r['before'][6] != published: raise ValueError('item-speed cap changed without publication')
            if phase in ('item_one', 'item_remove_two'): cached_commits += 1
    if len(actors) != 1 or not next(iter(actors))[1] or len(move_abilities) != 1 or len(movers) != 1 or len(epochs) > 1:
        raise ValueError('item-speed owner identity changed')
    if maximum_units != {next(iter(actors))[0]}: raise ValueError('item-speed maximum owner differs')
    one, two, remaining = [bonuses.get(k, set()) for k in ('item_one', 'item_two', 'item_remove_one')]
    if len(one) != 1 or len(two) != 2 or len(remaining) != 1 or not one < two or remaining != two - one:
        raise ValueError('item-speed attached bonus identity differs')
    if not cached_commits: raise ValueError('item-speed retained-cap commits missing')
    result = verify_motion(rows, engine, None)
    result.update(binary_sha256=fixture['binary_sha256'], speed_sha256=digest(rows),
                  exact_speed_compositions=exact_compositions, public_speed_calls=fixture['counts']['speed-native'],
                  cached_cap_commits=cached_commits, passed=True,
                  scope='Retail Hero Boots maximum on a RoC-format map and setter/order publication; other modifiers and full engine timing excluded')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--scene', choices=('quiet', 'publish'), required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--engine-library', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    engine = ctypes.CDLL(str(args.engine_library.resolve())); configure(engine)
    fixture = json.loads(args.fixture.read_text())['scenes'][args.scene]
    def capture(path): return verify([json.loads(s) for s in path.read_text().splitlines()], engine, fixture)
    result = capture(args.trace)
    if args.compare:
        repeat = capture(args.compare)
        for key in ('speed_sha256', 'decision_sha256', 'velocity_sha256'):
            if result[key] != repeat[key]: raise ValueError('item-speed repeat ' + key + ' differs')
        result['repeated'] = True
    args.report.write_text(json.dumps(result, indent=2) + '\n'); print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
