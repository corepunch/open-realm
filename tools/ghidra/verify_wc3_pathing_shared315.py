#!/usr/bin/env python3
"""Verify public exceptional Captain speeds, clock domains, and ordered shared owners."""
import argparse
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re

from verify_wc3_pathing_target_normalize218 import original_bytes, SHA

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'tools/ghidra/fixtures/retail-shared315-1.27.json'
BUNDLE = SPEC.with_suffix('.json.gz')
SCENES = ('0', '1', '2', '7')
WORDS = dict(zip(SCENES, (0x7fc00001, 0x7fa00001, 0xffc00001, 0)))
INSTRUCTION_COUNT = 1984
WIDTHS = {'motion': 8, 'public': 7, 'shared': 5}
TESTS = ['wc3_movement.shared315*', 'wc3_bot.shared315*',
         'wc3_movement.public_captain*', 'wc3_movement.shared189*',
         'wc3_movement.scheduler190*', 'wc3_movement.public_timer*', 'wc3_save.*']
SOURCES = ['tools/frida/research/shared315_' + suffix for suffix in
           ('capture.py', 'make_map.py', 'observer.js', 'timer_observer.js',
            'init_observer.js', 'probe.j', 'probe.ai')]
SOURCES += ['tools/frida/research/smart277_ui_input.c',
            'tools/frida/research/point214_ui_input.c', 'tools/frida/wc3_ui_input.c',
            'tools/ghidra/research/Shared315Evidence.java',
            'games/warcraft-3/game/tests/fixtures/retail_shared315.h',
            'games/warcraft-3/tests/resources-src/Scripts/shared315.j',
            'games/warcraft-3/tests/resources-src/Scripts/shared315.ai']


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def markers(preload):
    return re.findall(r'call Preload\( "(S315 [^"\r\n]*)" \)', preload)


def extract(rows):
    created = next(i for i, row in enumerate(rows)
                   if row['event'] == 'marker' and 'label=created' in row['value'])
    identities = []
    for row in rows[:created]:
        if row['event'] == 'physical-radius' and row['m']['id'] not in identities:
            identities.append(row['m']['id'])
    actors = identities[-3:]
    require(len(actors) == 3, 'missing physical actors')
    result = dict(motion=[], public=[], shared=[])
    coordinates, speeds = [], []
    for row in rows:
        event = row['event']
        if event == 'commit-leave':
            for member in row['g']['members']:
                if member['id'] in actors:
                    mover = member['m']
                    result['motion'].append([actors.index(member['id']), *mover['clock'],
                                             *mover['pos'], *mover['vel'], mover['heading']])
        elif event == 'public-coordinate':
            coordinates.append(row['value'])
        elif event == 'public-speed':
            speeds.append(row['value'])
        elif event == 'marker':
            unit = re.search(r'label=unit u=(\d+)', row['value'])
            if unit and coordinates:
                require(len(coordinates) == 2 and len(speeds) == 1, 'missing public getter')
                result['public'].append([row['tick'], int(unit[1]), row['clock'][0],
                                         *coordinates, *speeds,
                                         int(re.search(r'order=(\d+)', row['value'])[1])])
            coordinates, speeds = [], []
        elif event == 'shared-leave' and row['s']:
            shared = row['s']
            result['shared'].append([row['c'], shared['refs'], shared['published'],
                                     shared['next'], shared['radius']])
    return result


def periodic(rows):
    actors = [r['captain'] for r in rows if r['event'] == 'captain-init-enter']
    require(len(actors) == 2 and len(set(actors)) == 2, 'missing Captain initialization')
    events = [r for r in rows if r['event'] == 'captain-periodic-enter']
    require(len(events) == 16, 'missing periodic callback')
    require(all(len(r['clocks']['timers']) == 1 for r in events), 'wrong event clock domain')
    return [[actors.index(r['captain']), r['c'], r['clocks']['primary'][0],
             r['clocks']['timers'][0]['time']] for r in events]


def render_header(cases, clocks):
    lines = ['/* Frozen Payoff315 read-only retail observations; generated from paired complete captures. */']
    for scene in SCENES:
        for kind, width in WIDTHS.items():
            rows = cases[scene]['states'][kind]
            if kind == 'public':
                rows = rows[3:]  # The three pre-creation null-unit observations.
            lines.append(f'static uint32_t const shared315_{kind}_{scene}[][{width}]={{')
            lines.extend('    {' + ','.join(f'0x{word:08x}u' for word in row) + '},' for row in rows)
            lines.append('};')
    lines += ['', '/* Read-only timer-observe-2; repeat1 has the identical sixteen callbacks. */',
              'static uint32_t const shared315_periodic_clocks[][4]={']
    lines.extend('    {' + ','.join(f'0x{word:08x}u' for word in row) + '},' for row in clocks)
    lines.append('};')
    return '\n'.join(lines) + '\n'


def validate_capture(cap, spec, scene, mode, observer):
    rows, preload = cap['rows'], cap['preload']
    require(bool(rows), 'empty capture')
    meta, footer = rows[0], rows[-1]
    case = spec['cases'][scene]
    require(meta.get('event') == 'metadata' and meta.get('task') == 'NUM-03.2/shared315'
            and meta.get('sha256') == SHA and meta.get('owned') is True
            and meta.get('mode') == mode and meta.get('env') in ('B', 'C'), 'capture identity differs')
    require(meta['display'] == {'B': ':98', 'C': ':99'}[meta['env']]
            and meta['remote'] == {'B': '127.0.0.1:27049', 'C': '127.0.0.1:27050'}[meta['env']]
            and meta['source_sha256']['map'] == case['map_sha256'], 'capture environment/map differs')
    captured = ['shared315_capture.py', 'shared315_make_map.py', 'shared315_probe.j',
                'shared315_probe.ai', observer, 'smart277_ui_input.c', 'point214_ui_input.c']
    for name in captured:
        path = 'tools/frida/research/' + name
        require(meta['source_sha256'].get(name) == spec['pins'][path], 'captured source differs: ' + name)
    require(meta['source_sha256'].get('wc3_ui_input.c') == spec['pins']['tools/frida/wc3_ui_input.c'],
            'captured UI helper source differs')
    require(all(meta['source_sha256'].get(name) == value for name, value in spec['input_helpers'].items()),
            'owned loading helper differs')
    public = markers(preload)
    require(footer.get('event') == 'preload-file' and footer.get('complete') is True
            and not footer.get('missing') and footer.get('markers') == 368
            and len(public) == 368 and public == case['markers']
            and hashlib.sha256(preload.encode()).hexdigest() == footer.get('sha256'),
            'incomplete or changed public control')
    require(not any(r.get('type') == 'error' or r.get('event') in ('native-exception', 'trace-failed')
                    for r in rows), 'instrumentation failed')
    live = [r for r in rows if 'seq' in r and r['event'] != 'trace-end']
    ends = [r for r in rows if r['event'] == 'trace-end']
    if mode == 'control':
        require(not live and not ends, 'hooked control')
        return 0
    require([r['seq'] for r in live] == list(range(len(live))) and len(ends) == 1
            and ends[0].get('installed') is True and ends[0].get('readOnly') is True
            and ends[0]['seq'] == len(live)
            and ends[0]['counts'] == dict(Counter(r['event'] for r in live)), 'lost native events')
    require([r['value'] for r in live if r['event'] == 'marker' and r['tick'] != 1] == public,
            'live markers differ from generated preload')
    require([r['value'] for r in live if r['event'] == 'public-default-height'] == [WORDS[scene]]
            and [r['value'] for r in live if r['event'] == 'setter-enter'] == [WORDS[scene], 0x43870000],
            'public exceptional producer/restoration differs')
    require(extract(rows) == case['states'], 'ordered physical/public/shared stream differs')
    return len(live)


def validate(spec, bundle, check_sources=True):
    require(spec['version'] == 1 and spec['task'] == 'NUM-03.2' and spec['game_sha256'] == SHA
            and spec['engine_tests'] == TESTS and set(spec['cases']) == set(SCENES)
            and set(bundle) == {'cases', 'supplemental'} and set(bundle['cases']) == set(SCENES),
            'shared producer contract differs')
    require(len(spec['instructions']) == spec['instruction_count'] == INSTRUCTION_COUNT,
            'missing instruction guards')
    require(set(spec['pins']) == set(SOURCES), 'source inventory differs')
    if check_sources:
        require(digest(BUNDLE) == spec['bundle_sha256'], 'frozen bundle differs')
        for path, expected in spec['pins'].items():
            require(digest(ROOT / path) == expected, 'source differs: ' + path)
    native = 0
    for scene in SCENES:
        caps = bundle['cases'][scene]
        require(len(caps) == 3, 'missing observed repeat/control')
        for i, cap in enumerate(caps):
            native += validate_capture(cap, spec, scene, 'observe' if i < 2 else 'control',
                                       'shared315_observer.js')
        sizes = {k: len(v) for k, v in spec['cases'][scene]['states'].items()}
        require(sizes == {'motion': 863 if scene in ('0', '1') else 861, 'public': 273,
                          'shared': {'0': 257, '1': 257, '2': 196, '7': 195}[scene]},
                'truncated expected stream')
    supplements = bundle['supplemental']
    require(set(supplements) == {'timer-observe-2', 'init-observe-1', 'init-observe-2'},
            'missing clock-domain or initializer evidence')
    for label, cap in supplements.items():
        observer = 'shared315_timer_observer.js' if label.startswith('timer') else 'shared315_init_observer.js'
        native += validate_capture(cap, spec, '2', 'observe', observer)
        rows = cap['rows']
        require(periodic(rows) == spec['periodic_clocks'], 'periodic clock/order differs')
        enters = [r for r in rows if r['event'] == 'captain-range-enter-enter']
        require(len(enters) == 6 and all(r['c'] == 1130 and not r['clocks']['timers']
                                        and r['clocks']['primary'][0] == 0x404cccb0 for r in enters),
                'range callback clock domain differs')
        require([r['command'] for r in rows if r['event'] == 'captain-member-enter']
                == [851988] * 3 + [851986] * 3, 'prepared Captain commands differ')
        loads = [r for r in rows if r['event'] in ('ai-load-enter', 'ai-load-leave')]
        require(len(loads) == 2 and loads[0]['event'] == 'ai-load-enter', 'missing synchronous loader')
        for event in ('ai-thread-create-enter', 'ai-thread-init-enter', 'ai-thread-run-enter', 'captain-attack-enter'):
            require(any(r['event'] == event and loads[0]['seq'] < r['seq'] < loads[1]['seq']
                        for r in rows), 'startup was not synchronous: ' + event)
        if label.startswith('init'):
            require([r['order'] for r in rows if r['event'] == 'captain-init-leave'] == [851988, 851988],
                    'initial Captain command differs')
    require(len(spec['periodic_clocks']) == 16, 'missing periodic clock rows')
    if check_sources:
        require((ROOT / 'games/warcraft-3/game/tests/fixtures/retail_shared315.h').read_text()
                == render_header(spec['cases'], spec['periodic_clocks']), 'engine golden differs from retail')
    return dict(captures=15, native_events=native, public_markers=15 * 368,
                fresh_motion_rows=3448, fresh_public_rows=1080, fresh_shared_rows=905,
                periodic_rows=16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--test-binary', type=Path, default=ROOT / 'build/bin/openwarcraft3-tests')
    parser.add_argument('--data', type=Path, default=ROOT / 'build/tests')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        parser.error('new report required')
    spec = json.loads(SPEC.read_text())
    bundle = json.loads(gzip.decompress(BUNDLE.read_bytes()))
    evidence = validate(spec, bundle)
    original_bytes(args.binary, spec['instructions'])
    import verify_wc3_pathing_work242 as engine
    engine.TESTS = TESTS
    args.report.parent.mkdir(parents=True, exist_ok=True)
    result = dict(passed=True, status='verified', task=spec['task'], **evidence,
                  instructions=len(spec['instructions']), binary_sha256=SHA,
                  engine=engine.run_engine(args.test_binary, args.data, args.report),
                  fixture_sha256=digest(SPEC),
                  game_library_sha256=digest(args.test_binary.parent.parent / 'lib/libgame-wc3-test.so'),
                  limits=spec['limits'])
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
