#!/usr/bin/env python3
"""Bounded, read-only cursor trace; attach to an already running WC3 1.27 process."""
import argparse
import hashlib
import json
import struct
import time
from pathlib import Path

import frida


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--remote', default='127.0.0.1:27045')
    parser.add_argument('--pid', type=int, required=True, help='PID reported by Frida, not the Wine host PID')
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--sample-ms', type=int, default=250, help='0 records every cursor update')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--capture-dir', type=Path, help='capture one framebuffer pair around the cursor draw as BGRA rows')
    args = parser.parse_args()
    if not 0 < args.seconds <= 3600:
        parser.error('--seconds must be in (0, 3600]')
    if not 0 <= args.sample_ms <= 60000:
        parser.error('--sample-ms must be in [0, 60000]')
    binary = args.dll.read_bytes()
    digest = hashlib.sha256(binary).hexdigest()
    if digest != 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236':
        parser.error('unsupported game.dll; these offsets require WC3 1.27.1.7085')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    identity = {'timestamp': struct.unpack_from('<I', binary, pe + 8)[0],
                'imageSize': struct.unpack_from('<I', binary, pe + 80)[0]}
    device = frida.get_device_manager().add_remote_device(args.remote)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.capture_dir:
        args.capture_dir.mkdir(parents=True, exist_ok=True)
    with args.output.open('w') as output:
        errors = []

        def record(row):
            output.write(json.dumps(row) + '\n')
            output.flush()

        def message(msg, data):
            if data is not None and args.capture_dir and msg.get('payload', {}).get('event') == 'framebuffer':
                row = msg['payload']
                target = args.capture_dir / (row['stage'] + '.bgra')
                target.write_bytes(data)
                target.with_suffix('.json').write_text(json.dumps(row, indent=2))
            record(msg['payload'] if msg['type'] == 'send' else msg)
            if msg['type'] == 'error':
                errors.append(msg.get('description', str(msg)))

        record({'event': 'metadata', 'sha256': digest, 'pid': args.pid,
                'frida': frida.__version__, 'seconds': args.seconds, 'sampleMs': args.sample_ms, **identity})
        session = device.attach(args.pid)
        try:
            source = 'const expectedPE = ' + json.dumps(identity) + ';\n'
            source += 'const sampleInterval = ' + str(args.sample_ms) + ';\n'
            source += 'const captureFramebuffer = ' + str(bool(args.capture_dir)).lower() + ';\n'
            source += Path(__file__).with_name('wc3_cursor.js').read_text()
            script = session.create_script(source)
            script.on('message', message)
            script.load()
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline and not errors:
                time.sleep(0.1)
            record({'event': 'trace-end', **script.exports_sync.status()})
        except Exception as error:
            record({'event': 'trace-failed', 'error': str(error)})
            raise
        finally:
            session.detach()
        if errors:
            raise RuntimeError('; '.join(errors))


if __name__ == '__main__':
    main()
