#!/usr/bin/env python3
"""Bounded WC3 1.27 pathfinding observer; spawned processes are owned, attached ones are not."""
import argparse
import hashlib
import json
import struct
import os
import subprocess
import time
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
    parser.add_argument('--numeric-events', action='store_true', help='capture bracketed public scalar native outputs and decimal parser words')
    parser.add_argument('--literal-events', action='store_true', help='observe compiled long-decimal token words without invoking the compiler')
    parser.add_argument('--integer-events', action='store_true', help='observe compiled long integer token words from original decimal/octal/hex lexer actions')
    parser.add_argument('--byte-events', action='store_true', help='capture raw byte parser inputs and exact sibling CRT digit/locale observations')
    parser.add_argument('--task-events', action='store_true', help='observe point-task acceptance and arrival queue state')
    parser.add_argument('--motion-events', action='store_true', help='capture raw speed/heading decision bits for numerical replay')
    parser.add_argument('--velocity-events', action='store_true', help='capture raw velocity commits and selected simulation clocks')
    parser.add_argument('--heading-events', action='store_true', help='capture raw vector-to-heading errors')
    parser.add_argument('--profile-events', action='store_true', help='capture original unit movement categories, masks and bridge publication')
    parser.add_argument('--widget-events', action='store_true', help='observe widget methods after the widget lifecycle scenario starts')
    parser.add_argument('--blockers', action='store_true', help='aggregate original fine-cell blocker decisions per request')
    parser.add_argument('--watch-cell', type=int, nargs=2, metavar=('X', 'Y'), help='fine-grid cell and its parents at scenario markers')
    parser.add_argument('--x11-display', help='owned isolated X display for the loading-screen key')
    parser.add_argument('--continue-at', type=float, help='send Space once at this elapsed second; requires --x11-display')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 0 < args.seconds <= 3600 or not 0 < args.samples <= 10000:
        parser.error('seconds must be in (0, 3600], samples in [1, 10000]')
    if args.watch_cell and any(v < 0 or v > 65535 for v in args.watch_cell):
        parser.error('watch-cell coordinates must be in [0, 65535]')
    if args.continue_at is not None and (not args.x11_display or not 0 < args.continue_at < args.seconds or args.pid):
        parser.error('--continue-at requires an owned spawn, --x11-display, and a time within the capture')
    binary = (args.data / 'game.dll').read_bytes()
    if hashlib.sha256(binary).hexdigest() != HASH:
        parser.error('unsupported game.dll; requires mapped 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    config = {'timestamp': struct.unpack_from('<I', binary, pe + 8)[0],
              'imageSize': struct.unpack_from('<I', binary, pe + 80)[0], 'samples': args.samples,
              'watchCell': args.watch_cell, 'blockers': args.blockers, 'taskEvents': args.task_events,
              'widgetEvents': args.widget_events, 'motionEvents': args.motion_events,
              'velocityEvents': args.velocity_events, 'headingEvents': args.heading_events,
              'profileEvents': args.profile_events, 'numericEvents': args.numeric_events,
              'literalTexts': sorted({case['input'].lstrip('-') for case in json.loads(Path(__file__).with_name('wc3_literal_inputs.json').read_text())['cases'] if len(case['input'].lstrip('-')) > 10}) if args.literal_events else [],
              'integerTexts': sorted({case['input'].lstrip('-') for case in json.loads(Path(__file__).with_name('wc3_integer_inputs.json').read_text())['cases'] if len(case['input'].lstrip('-')) > 10}) if args.integer_events else [], 'byteEvents': args.byte_events}
    if args.byte_events:
        crt = (args.data / 'msvcr120.dll').read_bytes()
        crt_hash = hashlib.sha256(crt).hexdigest()
        if crt_hash != '86e39b5995af0e042fcdaa85fe2aefd7c9ddc7ad65e6327bd5e7058bc3ab615f':
            parser.error('byte capture requires the exact shipped sibling CRT')
        cp = struct.unpack_from('<I', crt, 0x3c)[0]
        config['crt'] = dict(sha256=crt_hash, timestamp=struct.unpack_from('<I', crt, cp+8)[0],
                             imageSize=struct.unpack_from('<I', crt, cp+80)[0],
                             path='Z:' + str((args.data / 'msvcr120.dll').resolve()).replace('/', '\\'))
    source_paths = [Path(__file__), Path(__file__).with_name('wc3_pathfinding.js'),
                    Path(__file__).with_name('wc3_pathfinding_probe.j'),
                    Path(__file__).with_name('make_wc3_pathfinding_map.py'),
                    Path(__file__).with_name('wc3_numeric_inputs.json'),
                    Path(__file__).with_name('wc3_angle_inputs.json'),
                    Path(__file__).with_name('wc3_power_inputs.json'),
                    Path(__file__).with_name('wc3_literal_inputs.json'),
                    Path(__file__).with_name('wc3_integer_inputs.json'),
                    Path(__file__).with_name('wc3_byte_inputs.json')]
    provenance = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in source_paths}
    map_path = args.data / args.map.replace('\\', '/')
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
    with args.output.open('w') as output:
        def record(row):
            output.write(json.dumps(row) + '\n')
            output.flush()

        def message(msg, _data):
            record(msg['payload'] if msg['type'] == 'send' else msg)
            if msg['type'] == 'error':
                errors.append(msg.get('description', str(msg)))

        try:
            pid = args.pid or device.spawn([path + r'\war3.exe', '-window', '-loadfile', args.map], cwd=path)
            record({'event': 'metadata', 'sha256': HASH, 'pid': pid, 'owned': args.pid is None,
                    'source_sha256': provenance, 'map': args.map, 'seconds': args.seconds, 'frida': frida.__version__, **config})
            session = device.attach(pid)
            source = 'const config = ' + json.dumps(config) + ';\n'
            source += Path(__file__).with_name('wc3_pathfinding.js').read_text()
            script = session.create_script(source)
            script.on('message', message)
            script.load()
            if args.pid is None:
                device.resume(pid)
            start = time.monotonic()
            deadline = start + args.seconds
            sent = False
            while time.monotonic() < deadline and not errors:
                if args.continue_at is not None and not sent and time.monotonic() - start >= args.continue_at:
                    subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', 'Warcraft III',
                                    'windowfocus', '--sync', 'key', 'space'], check=True, timeout=5,
                                   env={**os.environ, 'DISPLAY': args.x11_display}, stdout=subprocess.DEVNULL)
                    record({'event': 'loading-key', 'elapsed': time.monotonic() - start, 'key': 'space'})
                    sent = True
                time.sleep(0.1)
            record({'event': 'trace-end', **script.exports_sync.status()})
            if errors:
                raise RuntimeError('; '.join(errors))
        except Exception as error:
            record({'event': 'trace-failed', 'error': str(error)})
            raise
        finally:
            try:
                if session is not None:
                    session.detach()
            finally:
                if args.pid is None and pid is not None:
                    try:
                        device.kill(pid)
                    except frida.ProcessNotFoundError:
                        record({'event': 'owned-process-already-exited', 'pid': pid})


if __name__ == '__main__':
    main()
