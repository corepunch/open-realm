#!/usr/bin/env python3
"""Bounded WC3 1.27 pathfinding observer; spawned processes are owned, attached ones are not."""
import argparse
import hashlib
import json
import struct
import os
import subprocess
import time
import threading
from pathlib import Path

HASH = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def main():
    import frida
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--remote', default='127.0.0.1:27046')
    parser.add_argument('--pid', type=int, help='attach to this Windows/Frida PID; never terminate it')
    parser.add_argument('--map', default=r'Maps\Campaign\Human02Interlude.w3m')
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--samples', type=int, default=200)
    parser.add_argument('--random-movement-events', action='store_true', help='observe shared retry/overlap owner transactions and separation candidates')
    parser.add_argument('--random-events', action='store_true', help='observe public seed and owner random state before/after native queries')
    parser.add_argument('--numeric-events', action='store_true', help='capture bracketed public scalar native outputs and decimal parser words')
    parser.add_argument('--literal-events', action='store_true', help='observe compiled long-decimal token words without invoking the compiler')
    parser.add_argument('--integer-events', action='store_true', help='observe compiled long integer token words from original decimal/octal/hex lexer actions')
    parser.add_argument('--byte-events', action='store_true', help='capture raw byte parser inputs and exact sibling CRT digit/locale observations')
    parser.add_argument('--task-events', action='store_true', help='observe point-task acceptance and arrival queue state')
    parser.add_argument('--motion-events', action='store_true', help='capture raw speed/heading decision bits for numerical replay')
    parser.add_argument('--yield-events', action='store_true', help='observe ordered moving-blocker decisions, actual resolved groups and blocker handles')
    parser.add_argument('--velocity-events', action='store_true', help='capture raw velocity commits and selected simulation clocks')
    parser.add_argument('--clock-events', action='store_true', help='observe original clock subdivision and path-owner update order during the scenario')
    parser.add_argument('--scheduler-events', action='store_true', help='observe fine FIFO links, owner clocks and budget reset boundaries')
    parser.add_argument('--heading-events', action='store_true', help='capture raw vector-to-heading errors')
    parser.add_argument('--captain-approach-events', action='store_true', help='observe authored Captain AI approach ranges and enabled attack maxima without calling or changing game state')
    parser.add_argument('--captain-membership-events', action='store_true', help='observe Captain range departures, membership counters and private reissues without changing game state')
    parser.add_argument('--profile-events', action='store_true', help='capture original unit movement categories, masks and bridge publication')
    parser.add_argument('--fine-result-events', action='store_true', help='observe complete fine setup and caller results without invoking searches')
    parser.add_argument('--gate-marker-events',action='store_true',help='observe complete source marker/parent publication without reading unrelated movement')
    parser.add_argument('--gate-pool-events', action='store_true', help='observe original Way Gate availability, activation and public getters without mutating state')
    parser.add_argument('--adaptive-storage-events', action='store_true', help='observe original adaptive node/open/index storage without invoking searches')
    parser.add_argument('--fine-storage-events', action='store_true', help='observe original fine node/open storage growth without invoking searches')
    parser.add_argument('--blocker-patch-size', type=int, choices=(16,32), default=16, help='32 observes all static movement masks on a parent-aligned patch')
    parser.add_argument('--map-load-events', action='store_true', help='observe the complete file-backed loader before object creation')
    parser.add_argument('--widget-events', action='store_true', help='observe widget methods after the widget lifecycle scenario starts')
    parser.add_argument('--blockers', action='store_true', help='aggregate original fine-cell blocker decisions per request')
    parser.add_argument('--watch-cell', type=int, nargs=2, metavar=('X', 'Y'), help='fine-grid cell and its parents at scenario markers')
    parser.add_argument('--x11-display', help='owned isolated X display for the loading-screen key')
    parser.add_argument('--continue-at', type=float, help='send Space once at this elapsed second; requires --x11-display')
    parser.add_argument('--continue-after-start', type=float, help='send a second loading key this many seconds after the observed script start marker')
    parser.add_argument('--point-click-at', type=float, help='issue an explicit player Move using the owned X11 window at this elapsed second')
    parser.add_argument('--point-click', type=int, nargs=2, metavar=('X','Y'), help='window-relative pixel coordinates for the explicit Move click')
    parser.add_argument('--point-input-helper', type=Path, help='external Winelib SendInput helper; requires the explicit owned Move click')
    parser.add_argument('--point-click-alt', action='store_true', help='hold Alt using the owned Winelib input helper for formation policy')
    parser.add_argument('--point-click-shift', action='store_true', help='hold Shift using the owned Winelib input helper for the Move click')
    parser.add_argument('--point-click-extra', action='append', type=float, nargs=3, metavar=('AT','X','Y'), help='additional owned Winelib Move click; ordered time and integer client pixels')
    parser.add_argument('--point-click-from-start', action='store_true', help='time owned Move input from the observed scenario start marker')
    parser.add_argument('--point-native-key', action='store_true', help='send Move targeting through the reviewed native Shift helper')
    parser.add_argument('--point-click-sample-ticks', action='store_true', help='interpret click times as observed integer scenario sample ticks')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 0 < args.seconds <= 3600 or not 0 < args.samples <= 30000:
        parser.error('seconds must be in (0, 3600], samples in [1, 30000]')
    if args.watch_cell and any(v < 0 or v > 65535 for v in args.watch_cell):
        parser.error('watch-cell coordinates must be in [0, 65535]')
    if args.continue_at is not None and (not args.x11_display or not 0 < args.continue_at < args.seconds or args.pid):
        parser.error('--continue-at requires an owned spawn, --x11-display, and a time within the capture')
    if args.continue_after_start is not None and (args.continue_at is None or not 0 < args.continue_after_start < args.seconds):
        parser.error('--continue-after-start requires --continue-at and a positive in-capture delay')
    point_limit = 301 if args.point_click_sample_ticks else args.seconds
    if ((args.point_click_at is None) != (args.point_click is None) or
            (args.point_click_at is not None and (args.pid or not args.x11_display or
             not 0 < args.point_click_at < point_limit or min(args.point_click) < 0))):
        parser.error('--point-click-at/--point-click require an owned spawn, --x11-display and an in-capture time')
    if args.point_input_helper and (args.point_click_at is None or not args.point_input_helper.is_file()):
        parser.error('--point-input-helper requires an existing helper and an explicit owned Move click')
    if (args.point_click_shift or args.point_click_alt) and not args.point_input_helper:
        parser.error('--point-click-shift/--point-click-alt requires the owned Winelib input helper')
    if args.point_click_sample_ticks and (not args.point_input_helper or args.point_click_from_start):
        parser.error('--point-click-sample-ticks requires an owned helper and is separate from wall-clock start timing')
    if args.point_native_key and not args.point_input_helper:
        parser.error('--point-native-key requires the reviewed native input helper')
    if args.point_click_from_start and not args.point_input_helper:
        parser.error('--point-click-from-start requires an owned helper click')
    point_plan = []
    if args.point_click_at is not None:
        point_plan.append(dict(at=args.point_click_at, pixel=args.point_click))
    for at, x, y in args.point_click_extra or []:
        if (not args.point_input_helper or not point_plan or
                not point_plan[-1]['at'] < at < point_limit or
                min(x, y) < 0 or not x.is_integer() or not y.is_integer()):
            parser.error('--point-click-extra requires an ordered owned helper click within capture and integer pixels')
        point_plan.append(dict(at=at, pixel=[int(x), int(y)]))
    if args.point_click_sample_ticks and any(not p['at'].is_integer() for p in point_plan):
        parser.error('sample-tick input deadlines must be integers')
    binary = (args.data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        parser.error('unsupported game.dll; requires mapped 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    config = {'timestamp': struct.unpack_from('<I', binary, pe + 8)[0],
              'imageSize': struct.unpack_from('<I', binary, pe + 80)[0], 'samples': args.samples,
              'watchCell': args.watch_cell, 'blockerPatchSize': args.blocker_patch_size, 'blockers': args.blockers, 'taskEvents': args.task_events,
              'mapLoadEvents': args.map_load_events, 'widgetEvents': args.widget_events, 'motionEvents': args.motion_events,
              'velocityEvents': args.velocity_events, 'yieldEvents': args.yield_events, 'clockEvents': args.clock_events, 'headingEvents': args.heading_events,
              'schedulerEvents': args.scheduler_events,
              'captainApproachEvents': args.captain_approach_events, 'captainMembershipEvents': args.captain_membership_events,
              'profileEvents': args.profile_events, 'gatePoolEvents':args.gate_pool_events,'gateMarkerEvents':args.gate_marker_events, 'adaptiveStorageEvents': args.adaptive_storage_events, 'fineStorageEvents': args.fine_storage_events, 'fineResultEvents': args.fine_result_events, 'numericEvents': args.numeric_events, 'randomEvents': args.random_events,
              'literalTexts': sorted({case['input'].lstrip('-') for case in json.loads(Path(__file__).with_name('wc3_literal_inputs.json').read_text())['cases'] if len(case['input'].lstrip('-')) > 10}) if args.literal_events else [],
              'integerTexts': sorted({case['input'].lstrip('-') for case in json.loads(Path(__file__).with_name('wc3_integer_inputs.json').read_text())['cases'] if len(case['input'].lstrip('-')) > 10}) if args.integer_events else [], 'byteEvents': args.byte_events}
    if args.random_movement_events:
        config['randomMovementEvents']=True
    if args.byte_events:
        crt = (args.data / 'msvcr120.dll').read_bytes()
        crt_hash = hashlib.sha256(crt).hexdigest()
        if crt_hash != '86e39b5995af0e042fcdaa85fe2aefd7c9ddc7ad65e6327bd5e7058bc3ab615f':
            parser.error('byte capture requires the exact shipped sibling CRT')
        cp = struct.unpack_from('<I', crt, 0x3c)[0]
        config['crt'] = dict(sha256=crt_hash, timestamp=struct.unpack_from('<I', crt, cp+8)[0],
                             imageSize=struct.unpack_from('<I', crt, cp+80)[0],
                             path='Z:' + str((args.data / 'msvcr120.dll').resolve()).replace('/', '\\'))
    source_paths = [Path(__file__).with_name('wc3_formation_blocked_probe.j'), Path(__file__).with_name('wc3_formation_policy_probe.j'), Path(__file__).with_name('wc3_formation_rank_probe.j'), Path(__file__).with_name('wc3_mover_retirement_probe.j'), Path(__file__).with_name('wc3_scheduler_mutation_probe.j'), Path(__file__).with_name('wc3_scheduler_nonunit_probe.j'), Path(__file__).with_name('wc3_scheduler_probe.j'), Path(__file__).with_name('wc3_captain_probe.ai'), Path(__file__), Path(__file__).with_name('wc3_pathfinding.js'),
                    Path(__file__).with_name('wc3_pathfinding_probe.j'), Path(__file__).with_name('wc3_blocker_lifecycle_probe.j'), Path(__file__).with_name('wc3_widget_overlap_probe.j'), Path(__file__).with_name('wc3_adaptive_passage_probe.j'), Path(__file__).with_name('wc3_target_overlap_probe.j'), Path(__file__).with_name('wc3_terrain_cache_probe.j'), Path(__file__).with_name('wc3_movement_lifecycle_probe.j'), Path(__file__).with_name('wc3_region_callbacks_probe.j'), Path(__file__).with_name('wc3_movement_bypasses_probe.j'), Path(__file__).with_name('wc3_movement_modes_probe.j'), Path(__file__).with_name('wc3_speed_modifiers_probe.j'), Path(__file__).with_name('wc3_fine_results_probe.j'), Path(__file__).with_name('wc3_waygate_capacity_probe.j'), Path(__file__).with_name('wc3_waygate_overlap_probe.j'), Path(__file__).with_name('wc3_expression_probe.j'), Path(__file__).with_name('wc3_expression_inputs.json'),
                    Path(__file__).with_name('make_wc3_pathfinding_map.py'),
                    Path(__file__).with_name('wc3_numeric_inputs.json'),
                    Path(__file__).with_name('wc3_angle_inputs.json'),
                    Path(__file__).with_name('wc3_power_inputs.json'),
                    Path(__file__).with_name('wc3_literal_inputs.json'),
                    Path(__file__).with_name('wc3_integer_inputs.json'),
                    Path(__file__).with_name('wc3_byte_inputs.json')]
    if args.point_input_helper:
        helper_paths = [args.point_input_helper, Path(str(args.point_input_helper) + '.so'),
                        args.point_input_helper.with_name('wc3_ui_input.c')]
        if any(not p.is_file() for p in helper_paths):
            parser.error('owned Winelib input requires the helper, linked .so and reviewed C source')
        source_paths.extend(helper_paths)
        config['pointInput'] = dict(at=args.point_click_at, pixel=args.point_click, api='external Win32 SendInput', shift=args.point_click_shift, alt=args.point_click_alt)
        if len(point_plan)>1: config['pointInput']['extra'] = point_plan[1:]
        if args.point_click_from_start: config['pointInput']['fromStart'] = True
        if args.point_native_key: config['pointInput']['nativeKey'] = True
        if args.point_click_sample_ticks: config['pointInput']['sampleTicks'] = True
    provenance = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in source_paths}
    map_path = args.data / args.map.replace('\\', '/')
    if args.random_movement_events:
        extension=Path(__file__).with_name('wc3_pathfinding_random_movement.js')
        provenance[extension.name]=hashlib.sha256(extension.read_bytes()).hexdigest()
    captain_ai = map_path.with_suffix('.ai')
    if captain_ai.is_file():
        provenance['captain_ai'] = hashlib.sha256(captain_ai.read_bytes()).hexdigest()
    if args.numeric_events and not map_path.is_file():
        parser.error('numeric capture requires the actual map for provenance hashing')
    if map_path.is_file():
        provenance['map'] = hashlib.sha256(map_path.read_bytes()).hexdigest()
    device = frida.get_device_manager().add_remote_device(args.remote)
    path = 'Z:' + str(args.data.resolve()).replace('/', '\\')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    session = None
    pid = None
    errors = []
    scenario_start = None
    scenario_tick = 0
    with args.output.open('w') as output:
        record_lock = threading.Lock()
        record_count = 0

        def record(row):
            nonlocal record_count
            with record_lock:
                output.write(json.dumps(row) + '\n')
                record_count += 1
                # Per-record flushing delays marker-driven input behind the game.
                # Preserve every record and flush bounded batches and terminal rows.
                if record_count % 128 == 0 or row.get('event') in ('metadata', 'trace-end', 'trace-failed') or row.get('type') == 'error':
                    output.flush()

        def message(msg, _data):
            nonlocal scenario_start, scenario_tick
            payload = msg['payload'] if msg['type'] == 'send' else msg
            record(payload)
            if payload.get('event') == 'marker' and payload.get('value', '').startswith('PATHTRACE tick='):
                scenario_tick = int(payload['value'].split(' ', 2)[1][5:])
            if (scenario_start is None and payload.get('event') == 'marker' and
                    'PATHTRACE tick=0 label=start_' in payload.get('value', '')):
                scenario_start = time.monotonic()
            if msg['type'] == 'error':
                errors.append(msg.get('description', str(msg)))

        try:
            pid = args.pid or device.spawn([path + r'\war3.exe', '-window', '-loadfile', args.map], cwd=path)
            record({'event': 'metadata', 'sha256': HASH, 'pid': pid, 'owned': args.pid is None,
                    'source_sha256': provenance, 'map': args.map, 'seconds': args.seconds, 'frida': frida.__version__, **config})
            session = device.attach(pid)
            source = 'const config = ' + json.dumps(config) + ';\n'
            source += Path(__file__).with_name('wc3_pathfinding.js').read_text()
            if args.random_movement_events:
                source += '\n'+extension.read_text()
                anchor='    const hook = (rva, callbacks) => Interceptor.attach(base.add(rva), callbacks);'
                if source.count(anchor)!=1:raise RuntimeError('shared-owner observer installation point differs')
                source=source.replace(anchor,anchor+'\n    installPathRandomMovement(base,hook);',1)
            script = session.create_script(source)
            script.on('message', message)
            script.load()
            if args.pid is None:
                device.resume(pid)
            start = time.monotonic()
            deadline = start + args.seconds
            sent = False
            marker_key_sent = False
            clicked = 0
            while time.monotonic() < deadline and not errors:
                fixed_key = args.continue_at is not None and not sent and time.monotonic() - start >= args.continue_at
                marker_key = args.continue_after_start is not None and not marker_key_sent and scenario_start is not None and time.monotonic() - scenario_start >= args.continue_after_start
                if fixed_key or marker_key:
                    subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III',
                                    'windowfocus', '--sync', 'key', 'space'], check=True, timeout=5,
                                   env={**os.environ, 'DISPLAY': args.x11_display}, stdout=subprocess.DEVNULL)
                    record({'event': 'loading-key', 'elapsed': time.monotonic() - start,
                            'from_script_start': marker_key, 'key': 'space'})
                    if fixed_key: sent = True
                    if marker_key: marker_key_sent = True
                point_start = scenario_start if args.point_click_from_start else start
                if (clicked < len(point_plan) and
                        ((args.point_click_sample_ticks and scenario_tick >= point_plan[clicked]['at']) or
                         (not args.point_click_sample_ticks and point_start is not None and
                          time.monotonic() - point_start >= point_plan[clicked]['at']))):
                    env = {**os.environ, 'DISPLAY': args.x11_display}
                    windows = subprocess.check_output(['xdotool','search','--onlyvisible','--name','Warcraft III'], env=env, timeout=5).decode().splitlines()
                    if len(windows) != 1:
                        raise RuntimeError('explicit Move click requires exactly one owned-display Warcraft window')
                    if not args.point_native_key:
                        subprocess.run(['xdotool','windowfocus','--sync',windows[0],'key','m','sleep','0.3'],
                                       check=True, timeout=5, env=env, stdout=subprocess.DEVNULL)
                    if args.point_input_helper:
                        helper_output = subprocess.check_output([str(args.point_input_helper.resolve()), str(pid),
                            *[str(v) for v in point_plan[clicked]['pixel']], *(['shift'] if args.point_click_shift else []), *(['alt'] if args.point_click_alt else []), *(['move'] if args.point_native_key else [])], env=env, timeout=10).decode()
                        record({'event':'player-input-helper','output':helper_output,
                                'sha256':hashlib.sha256(args.point_input_helper.read_bytes()).hexdigest()})
                    else:
                        subprocess.run(['xdotool','mousemove','--window',windows[0],*[str(v) for v in point_plan[clicked]['pixel']],
                                        'mousedown','1','sleep','0.2','mouseup','1'], check=True, timeout=5, env=env, stdout=subprocess.DEVNULL)
                    record({'event':'player-move-click','elapsed':time.monotonic()-start,
                            'pixel':point_plan[clicked]['pixel'],'key':'m','button':1,'shift':args.point_click_shift,'alt':args.point_click_alt})
                    clicked += 1
                time.sleep(0.1)
            record({'event': 'trace-end', **script.exports_sync.finish()})
            if errors:
                raise RuntimeError('; '.join(errors))
        except Exception as error:
            record({'event': 'trace-failed', 'error': str(error)})
            raise
        finally:
            try:
                # An owned target may keep entering hot hooks while detach waits.
                # Finish freezes the footer first; kill only our spawned process.
                if args.pid is None and pid is not None:
                    try:
                        device.kill(pid)
                    except frida.ProcessNotFoundError:
                        record({'event': 'owned-process-already-exited', 'pid': pid})
            finally:
                if session is not None:
                    session.detach()


if __name__ == '__main__':
    main()
