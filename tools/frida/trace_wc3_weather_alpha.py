#!/usr/bin/env python3
"""Bounded read-only retail GL trace for Prologue01's RLhr rain alpha path."""
import argparse
import hashlib
import json
import time
from pathlib import Path

import frida


EXPECTED_SHA256 = '3f2ed0120d80578bf07e4423296dade1adfb959d59a2d20a7584224559570eed'


def validate_controller_inputs(exe, seconds):
    if not 0 < seconds <= 300:
        raise ValueError('--seconds must be in (0, 300]')
    binary_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    if binary_hash != EXPECTED_SHA256:
        raise ValueError(f'unsupported Warcraft III.exe SHA256: {binary_hash}')
    return binary_hash


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True, help='hash-checked Warcraft III.exe')
    parser.add_argument('--remote', default='127.0.0.1:27043', help='Frida server address')
    parser.add_argument('--pid', type=int, help='Frida/Windows PID; defaults to Warcraft III.exe')
    parser.add_argument('--seconds', type=float, default=10.0)
    parser.add_argument('--output', type=Path, required=True, help='JSONL destination')
    args = parser.parse_args()

    try:
        binary_hash = validate_controller_inputs(args.exe, args.seconds)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    device = frida.get_device_manager().add_remote_device(args.remote)
    target = device.get_process(args.pid) if args.pid else device.get_process('Warcraft III.exe')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    errors = []

    with args.output.open('w', encoding='utf-8') as output:
        def record(row):
            output.write(json.dumps(row, sort_keys=True) + '\n')
            output.flush()

        def on_message(message, _data):
            if message['type'] == 'send':
                record(message['payload'])
            else:
                record({'event': 'frida-error', 'message': message})
                if message['type'] == 'error':
                    errors.append(message.get('description', str(message)))

        record({
            'event': 'metadata',
            'executable': str(args.exe.resolve()),
            'sha256': binary_hash,
            'frida': frida.__version__,
            'remote': args.remote,
            'pid': target.pid,
            'seconds': args.seconds,
        })
        session = device.attach(target.pid)
        try:
            script = session.create_script(Path(__file__).with_name('wc3_weather_alpha.js').read_text(encoding='utf-8'))
            script.on('message', on_message)
            script.load()
            deadline = time.monotonic() + args.seconds
            started = time.monotonic()
            while time.monotonic() < deadline and not errors:
                time.sleep(0.1)
            record({
                'event': 'trace-end',
                'elapsedSeconds': round(time.monotonic() - started, 3),
                'rainDrawEvents': script.exports_sync.eventcount(),
            })
        finally:
            session.detach()

    if errors:
        raise RuntimeError('; '.join(errors))


if __name__ == '__main__':
    main()
